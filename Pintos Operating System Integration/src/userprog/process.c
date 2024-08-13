#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

static thread_func start_process NO_RETURN;
static struct lock file_lock;
bool lock_initialized = false;

struct args {
  int argc;
  char *argv[128];
};

// Stores information about the child of the parent such that the 
// parent always knows it child's exit status. 
struct wait_info {
  struct list_elem wait_elem;
  tid_t child_tid;
  int exit_status;
};

static bool load (struct args *args, void (**eip) (void), void **esp) ;

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  if (!lock_initialized) {
    lock_init(&file_lock);
    lock_initialized = true;
  }
  if (thread_current()->parent == NULL) {
    thread_current()->working_dir = dir_open_root();
  }
  char *fn_copy;
  tid_t tid;

  // Process filename
  // Create thread, which is a child to the current thread
  char temp_filename[16];
  memcpy(temp_filename, file_name, 16);

  char *ptr;
  char *argv = strtok_r(temp_filename," ", &ptr);
  

  /* Intial open check to see if this argv is a valid file/executable, 
  if not, then I want to immediately return -1. */
  struct file *temp = filesys_open(argv);
  if (temp == NULL) {
    return -1;
  }
  else {
     // Need to close if it is a valid file to prevent fd duplicates
    file_close(temp);
  }

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL) {
    return TID_ERROR;
  }
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (argv, PRI_DEFAULT, start_process, fn_copy);
  struct semaphore sema;
  sema_init(&sema, 0);
  
  struct thread *new_child_thread;
  if (tid == TID_ERROR) {
    palloc_free_page (fn_copy); 
  }
  else {
    // Finds the child that was just created from thread_create
    struct list_elem *new_child = NULL;
    struct list_elem *cur_lst;
    struct thread *cur = thread_current();
    for (cur_lst = list_begin(&(cur->allelem)); cur_lst != list_end(&(cur->allelem)); cur_lst=list_next(cur_lst)) {
      struct thread *cur_child = list_entry(cur_lst, struct thread, allelem);
      if (cur_child->tid == tid) {
        new_child = cur_lst;
        new_child_thread = cur_child;
        break;
      }
    }

    // Pushes the child onto the current thread's child of children, and also properly 
    // intalizes the wait_info struct for the parent-child relationship. 
    if (new_child) {
      list_push_back(&(cur->children), &(new_child_thread->child_elm));
      new_child_thread->parent = cur;
      new_child_thread->working_dir = cur->working_dir;
      struct wait_info *wait;
      wait = palloc_get_page(0);
      // Memory check, to see if we have run out of memory
      if (wait==NULL) {
        return -1;
      }
      memset(wait, 0, sizeof(*wait));
      (wait)->child_tid = tid;
      // Pushes wait_info struct onto the parent's child_statuses list which stores all of them
      list_push_back(&(cur->child_statuses), &((wait)->wait_elem));
      thread_current()->load_sema = &sema;
      // This is load_sema, we sema_down and then wait for it to be properly loaded in, 
      // if it didn't, then we return -1.
      sema_down(&sema);
      if (!thread_current()->load_success) {
        list_init(&cur->children);
        return -1;
      }      
    }
  }
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{

  char *filename = (char *) file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  struct args args;

  char *ptr = filename;
  int argc = 0;
  char *arg;

  while ((arg = strtok_r(ptr, " ", &ptr))) {
    (&args)->argv[argc] = arg;
    argc++;
  }

  (&args)->argc = argc;

  success = load (&args, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  palloc_free_page ((&args)->argv[0]);
  (thread_current()->parent)->load_success = success;
  sema_up((thread_current()->parent)->load_sema);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  // Invalid tid check
  if (child_tid < 0) {
    return -1;
  }

  // Needs to happen in interrupt disabled context
  intr_disable();
  struct thread *cur = thread_current ();
  struct thread *wait_child = NULL;
  struct list_elem *cur_lst;
  if(!list_empty(&(cur->children))) {
    for (cur_lst = list_begin(&(cur->children)); cur_lst != list_end(&(cur->children)); cur_lst=list_next(cur_lst)) {
      struct thread *cur_child = list_entry(cur_lst, struct thread, child_elm);
      if (cur_child->tid == child_tid) {
        wait_child = cur_child;
        break;
      }
    }
  }

  struct wait_info *wait_info_child = NULL;
  struct list_elem *cur_lst2;
  // Following two while loops find the current children and its current wait_info equivalent, 
  // which will provide us with all the information we need to see if the child exited already 
  // and if so, then with what status.
  if(!list_empty(&(cur->child_statuses))) {
    for (cur_lst2 = list_begin(&(cur->child_statuses)); cur_lst2 != list_end(&(cur->child_statuses)); cur_lst2=list_next(cur_lst2)) {
      struct wait_info *cur_info = list_entry(cur_lst2, struct wait_info, wait_elem);
      if (cur_info->child_tid == child_tid) {
        wait_info_child = cur_info;
        break;
      }
    }
  }

  // In this case, this id was never our child
  if (!wait_child && !wait_info_child) {
    intr_enable();
    return -1;
  }

  // In this case, the child has already exited, and we clean up its memory 
  // and return with its exit status.
  if (!wait_child && wait_info_child) {
    intr_enable();
    
    int exit_status = wait_info_child->exit_status;
    list_remove(&(wait_info_child->wait_elem));
    palloc_free_page(wait_info_child);
    return exit_status;
  }

  // In this case, the child still exists, so we sema_down and essentially wait
  // until the child exits, to sema_up on this semaphore that the parent and child
  // both share. After the sema_up gets called, the parent properly sets its status 
  // and frees the memory associated with the child. 

  intr_enable();
  struct semaphore sema;
  sema_init(&sema, 0);
  thread_current()->child_sema = &sema;
  wait_child->parent_sema = &sema;
  sema_down(&sema); 
  
  int ret_status = wait_info_child->exit_status;

  list_remove(&(wait_info_child->wait_elem));
  palloc_free_page(wait_info_child);
  return ret_status;
}

/* Free the current process's resources. */
void
process_exit(int status)  // NOTE ADDED STATUS HERE NEED TO MAKE SURE IT HANDLES THIS PROPERLY
{
  intr_disable();
  struct thread *cur = thread_current ();
  uint32_t *pd;
  
  ASSERT(!intr_context());
  
  printf("%s: exit(%d)\n", cur->name, status);
  
  // Go through the list of children and set the parent field to null
  if(!list_empty(&(cur->children))) {
    struct list_elem *cur_lst;
    for (cur_lst = list_begin(&(cur->children)); cur_lst != list_end(&(cur->children)); cur_lst=list_next(cur_lst)) {
      struct thread *cur_child = list_entry(cur_lst, struct thread, allelem);
      cur_child->parent = NULL; 
    }
  }

  // Go through the list of children's child statuses and free them
  if(!list_empty(&(cur->child_statuses))) {
    struct list_elem *cur_lst2;
    for (cur_lst2 = list_begin(&(cur->child_statuses)); cur_lst2 != list_end(&(cur->child_statuses)); cur_lst2=list_next(cur_lst2)) {
      struct wait_info *cur_info = list_entry(cur_lst2, struct wait_info, wait_elem);
      list_remove(&(cur_info->wait_elem));
      if (cur_info != NULL) {
        palloc_free_page(cur_info);
        break;
      }
    }
  }
  
  // If parent is currently waiting for you, update your wait status
  // and sema up for your parent can run
  if(thread_current()->parent != NULL && thread_current()->parent_sema!=NULL) 
  {
    struct wait_info *wait_child = NULL;
    struct list_elem *cur_lst3;
    struct list_elem *wait_child_elem;
    struct thread *parent = thread_current()->parent;

    if(!list_empty(&(parent->child_statuses))) {
      for (cur_lst3 = list_begin(&(parent->child_statuses)); cur_lst3 != list_end(&(parent->child_statuses)); cur_lst3=list_next(cur_lst3)) {
        struct wait_info *child_info = list_entry(cur_lst3, struct wait_info, wait_elem);
        if (child_info->child_tid == thread_current()->tid) {
          wait_child = child_info;
          wait_child_elem = cur_lst3;
          break;
        }
      }
    }
    wait_child->exit_status = status;
    list_remove(&(thread_current()->child_elm));
    sema_up(thread_current()->parent_sema);
  }

  // If the parent has not exited yet and the parent is not waiting for you,
  // update your exit status for your parent so that your parent can access it
  // later
  else if(thread_current()->parent != NULL) {
    struct wait_info *wait_child = NULL;
    struct list_elem *cur_lst2;
    struct list_elem *wait_child_elem;
    struct thread *parent = thread_current()->parent;

    if(!list_empty(&(parent->child_statuses))) {
      for (cur_lst2 = list_begin(&(parent->child_statuses)); cur_lst2 != list_end(&(parent->child_statuses)); cur_lst2=list_next(cur_lst2)) {
        struct wait_info *child_info = list_entry(cur_lst2, struct wait_info, wait_elem);
        if (child_info->child_tid == thread_current()->tid) {
          wait_child = child_info;
          wait_child_elem = cur_lst2;
          break;
        }
      }
    }
    if (wait_child != NULL) {
      wait_child->exit_status = status;
    }
    list_remove(&(thread_current()->child_elm));
  }

  // Close all of your files
  for (size_t i=2; i < 128; i++) {
    if(thread_current()->fileArray[i] != NULL) {
      file_close(thread_current()->fileArray[i]);
    }
  }
  
  // Close your executable
  if (thread_current()->executed != NULL) {
    file_close(thread_current()->executed);
  }
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
  intr_enable();
  }

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, struct args *args);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool load (struct args *args, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (args->argv[0]);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", args->argv[0]);
      goto done; 
    }
  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", args->argv[0]);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, args))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  if (file == NULL) {
    thread_exit(-1);
  }
  file_deny_write(file);
  t->executed = file;
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL) {
        return false;
      }

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, struct args *args) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success) {
        *esp = PHYS_BASE;
      }
      else {
        palloc_free_page (kpage);
      }

  }
  else {
    return false;
  }
  
  char *addresses[args->argc];
  int word_align = 0;
  for (int i=args->argc - 1; i >= 0; i--) {
    *esp -= (strlen(args->argv[i]) + 1);
    memcpy(*esp, args->argv[i], strlen(args->argv[i]) + 1);
    addresses[i] = *esp;
    word_align += strlen(args->argv[i]) + 1;
  }

  if (word_align % 4 != 0) {
    int word_add = (4 - word_align % 4);

    for (int i=0; i < word_add; i++) {
      *esp -= sizeof(char);
      *((uint8_t *)*esp) = (uint8_t) 0;
    }
  }
  *esp -= sizeof(char *);
  *((char *)*esp) = '\0';

  void *stack_ptr;
  for (int i=args->argc - 1; i >= 0; i--) {
    *esp -= sizeof(char *);
    memcpy(*esp, &(addresses[i]), sizeof(char *));
    if (i == 0) {
      stack_ptr = *esp;
    }
  }

  *esp -= sizeof(char *);
  memcpy(*esp, &stack_ptr, sizeof(char *));
  int argc_num = args->argc;
  *esp -= sizeof(int);
  *((int *)*esp) = argc_num;

  *esp -= sizeof(void *);
  *((void **)*esp) = 0;

  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}