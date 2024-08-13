#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "userprog/exception.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "filesys/inode.h"

static void syscall_handler (struct intr_frame *);
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
} 

bool validate(void *ptr);
int get_sysnum(void *esp);
bool check_string(void *ptr);
bool check_buffer(void *ptr, unsigned size);
bool set_dir_entry(const char *path, unsigned initial_size, bool is_dir);

static void
syscall_handler (struct intr_frame *f) 
{
  // Ensures valid esp pointer
  if (!validate(f->esp)) {
    exit(-1);
    return;
  }
  void *esp = f->esp;
  void *arg1;
  
  // Ensures valid syscall_number
  int syscall_num = get_sysnum(esp);
  if (syscall_num == -1) {
    exit(-1);
  }
  int off1, off2;

  // Intializes address for arg1, and allows for arg2/3 to be intialized
  arg1 = esp + sizeof(int);
  void *arg2;
  void *arg3;

  switch(syscall_num) {
    case SYS_HALT:
      halt();
      break;

    case SYS_EXIT:
      if (!validate(arg1)) {
        exit(-1);
      }
      else {
        exit(*(int *) arg1);
      }
      break;

    case SYS_EXEC:
      if (!validate(arg1)) {
        exit(-1);
      }
      if (!check_string(arg1)){
        thread_exit(-1);
      }
      if (!check_string(*(void **)arg1)) {
        exit(-1);
      }
      else {
      f->eax = (uint32_t) exec(*((char **) arg1));
      }
      break;

    case SYS_WAIT:      
      if (!validate(arg1)) {
        exit(-1);
      }
      else {
      f->eax = (uint32_t) wait(*((pid_t *) arg1));
      }
      break;

    case SYS_CREATE:
      arg2 = arg1 + sizeof(char *);

      if(!validate(arg1) || !validate(arg2)) {
        exit(-1);
      }
      if (!check_string(*(void **) arg1)) {
        exit(-1);
      }
      else {
        f->eax = (uint32_t) create(*((char **) arg1), *((unsigned int *) arg2));
      }
      break;

    case SYS_REMOVE:
      if (!validate(arg1)) {
        exit(-1);
      }
      else {
        f->eax = (uint32_t) remove(*((char **) arg1));
      }
      break;


    case SYS_OPEN:
      if (!validate(arg1) || (*(void **) arg1) == NULL) {
        exit(-1);
      }
      if (!check_string(*(void **) arg1)) {
        exit(-1);
      }
      else {
        f->eax = (uint32_t) open(*((char **) arg1));
      }
      break;

    case SYS_FILESIZE:
      if (!validate(arg1)) {
        exit(-1);
      }
      else {
        f->eax = (uint32_t) filesize(*(int *) arg1);
      }
      break;

    case SYS_READ:
      arg2 = arg1 + sizeof(int);
      arg3 = arg2 + sizeof(void *);

      if (!validate(arg1) || !validate(arg2) || !validate(arg3)) {
        exit(-1);
      }
      if (*(void **) arg1 == NULL) {
        exit(-1);
      }
      if (!check_buffer(*((void **) arg2), *((unsigned int *) (arg3)))) {
        exit(-1);
      }
      else {
        f->eax = (uint32_t) read(*((int *) arg1), *((void **) (arg2)), *((unsigned *) arg3));
      }
      break;
      
    case SYS_WRITE:

      arg2 = arg1 + sizeof(int);
      arg3 = arg2 + sizeof(void *);

      if (!validate(arg1) || !validate(arg2) || !validate(arg3)) {
        exit(-1);
      }
      else {
        f->eax = (uint32_t) write(*((int *) arg1), *((void **) (arg2)), *((unsigned int *) (arg3)));
      }
      break;

    case SYS_SEEK:
      arg2 = arg1 + sizeof(int);

      if (!validate(arg1) || !validate(arg2)) {
        exit(-1);
      }
      else {
        seek(*((int *) arg1), *((unsigned *) arg2));
      }
      break;

    case SYS_TELL:
      if (!validate(arg1)) {
        exit(-1);
      }
      else {
        f->eax = (uint32_t) tell(*((int *) arg1));
      }
      break;

    case SYS_CLOSE:
      if (!validate(arg1)) {
        exit(-1);
      }
      else {
        close(*((int *) arg1));
      }
      break;

    case SYS_CHDIR: 
      if (!validate(arg1)) {
        exit(-1);
      }
      else {
        f->eax = (uint32_t) chdir(*((char **) arg1));
      }
      break;

    case SYS_MKDIR: 
      if (!validate(arg1)) {
        exit(-1);
      }
      else {
        f->eax = (uint32_t) mkdir(*((char **) arg1));
      }
      break;
    
    case SYS_READDIR:
      arg2 = arg1 + sizeof(int);
      if (!validate(arg1) || !validate(arg2)) {
        exit(-1);
      }
      else {
        f->eax = (uint32_t) readdir(*((int *) arg1), *((char **) (arg2)));
      }
      break;
    
    case SYS_ISDIR:
      if (!validate(arg1)) {
        exit(-1);
      }
      else {
        f->eax = isdir(*((int *) arg1));
      }
      break;
    case SYS_INUMBER:
      if (!validate(arg1)) {
        exit(-1);
      }
      else {
        f->eax = inumber(*((int *) arg1));
      }
      break;
  }
}

/* 
* Validates given pointer is valid 
* Additional validation is done in page_fault handler
* when the pointer is accessed
*/
bool validate(void *ptr) {
  if (ptr == NULL || !is_user_vaddr(ptr)) {
    return false;
  }
  if (pagedir_get_page(thread_current ()->pagedir, ptr) == NULL) {
    return false;
  }
  return true;
}

/* 
* Ensures that given buffer to read from is valid
*/
bool check_string(void *ptr) {
  void *cur_ptr = ptr;
  bool validated = false;
  while(validate(cur_ptr)) {
    if (*(char *)cur_ptr == '\0') {
      validated = true;
      break;
    }
    cur_ptr += 1;
  }
  return validated;
}

/* 
* Ensures that given buffer to write to is valid
*/
bool check_buffer(void *ptr, unsigned size) {
  void *cur_ptr = ptr;
  bool validated = false;
  unsigned i = 0;
  while(i < size) {
    if (!validate(cur_ptr)) {
      return false;
    }
    cur_ptr += 1;
    i += 1;
  }
  return true;
}

/* 
* Validates address is valid and returns syscall number
*/
int get_sysnum(void *esp) {
  if (!validate(esp)) {
    return -1;
  }
  for (int i=0; i < sizeof(int); i++) {
    if (!validate(esp + i)) {
      return -1;
    }
  }
  int cur_addr = *(int*)esp;
  return cur_addr;
}

/* Syscall halt: calls shutdow_power_off() */
void halt(void) {
  shutdown_power_off();
}

/* Syscall exit: calls exit() */
void exit(int status) {
  thread_exit(status);
}

/* Syscall create: creates a file with given size */
// Updated create to use set_dir_entry, passed in with false, as this
// specifically is for files
bool create(const char *file, unsigned initial_size) {
  if (file == NULL) {
    exit(-1);
  }
  bool status = set_dir_entry(file, initial_size, false);
  return status;
}

/* Syscall remove: removes given file */
bool
remove (const char *file)
{
 if (file == NULL) {
  exit(-1);
 }
 // dir_remove within filesys_remove handles directory cases
 return filesys_remove(file);
}

/* Syscall exec: execs given command line by calling process_execute */
pid_t exec(const char *cmd_line) {
  pid_t cur_pid = (int) process_execute(cmd_line);
  return cur_pid;
}

/* Syscall wait: waits on a given pid */
int wait(pid_t pid) {
  return process_wait(pid);
}

/* Syscall open: opens a file with given name */
int open(const char *file) {
  if (file == NULL) {
    return -1;
  }
  
  struct file *f;
  int num_fds = thread_current()->num_fds;
  if (num_fds == 128) {
    return -1;
  }
  struct inode *inode = NULL;
  // Filesys_open now properly validates the path to ensure directories can be opened
  f = filesys_open(file);
  if (f == NULL) {
    return -1;
  }

  int next_fd;
  for (size_t i=2; i < 128; i++) {
    if(thread_current()->fileArray[i] == NULL) {
      thread_current()->fileArray[i] = f;
      next_fd = i;
      break;
    }
  }
  thread_current()->num_fds += 1;
  return next_fd;
}

/* Syscall filesize: gets size of file with given file descriptor */
int
filesize (int fd) 
{
  if (fd < 2 || fd > 128) {
    return 0;
  }
  off_t fsize;
  struct file *cur_file = (thread_current()->fileArray)[fd];
  if (cur_file) {
    fsize = file_length(cur_file);
    return (int) fsize;
  }
  else {
    return 0;
  }
}

/* Syscall read: reads from a file into given buffer */
int
read (int fd, void *buffer, unsigned size)
{
  if (fd == 0) {
    while (size > 0) {
      buffer = (void *) input_getc();
      buffer += sizeof(uint8_t);
      size--;
    }
    return size;
  }
  else {
    if (fd == 1 || fd > 128 || fd < 0) {
      return -1;
    }
    struct file *cur_file = (thread_current()->fileArray)[fd];
    if (cur_file == NULL) {
      return -1;
    }
    off_t bytes_read = file_read(cur_file, buffer, (off_t) size);
    return bytes_read;
  }
}

/* Syscall write: writes from given file descriptor into buffer */
int write (int fd, const void *buffer, unsigned size) {
  if (!check_buffer(buffer, size)) {
    exit(-1);
  }
  int written = 0;
  int cur_index = 0;
  off_t write_loc;
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }
  if (fd < 128 && fd > 0) {
    struct file *cur_file = (thread_current()->fileArray)[fd];
    if (cur_file == NULL) {
      return 0;
    }
    // Ensures that a directory cannot be written to
    if (dir_or_no(cur_file)) {
      return -1;
    }
    if (!file_get_allow(cur_file)) {
      bool already = false;
      if (inode_get_inumber(file_get_inode(cur_file)) == inode_get_inumber(file_get_inode(thread_current()->executed))) {
        already = true;
      }
      if (!already) {
        written = file_write(cur_file, buffer, (off_t) size);
      }
    }
  return written;
  }
}

/* Syscall seek: moves file position to given position */
void seek(int fd, unsigned position) {  
  int cur = 0;
  if (fd < 2 || fd > 128 || !fd) {
    return;
  }
  struct file *val = thread_current()->fileArray[fd];
  if (val == NULL) {
    return;
  }

  file_seek(val, position);
}

/* Syscall tell: returns position of the given file descriptor */
unsigned
tell (int fd) 
{
  if (fd < 2 || fd > 15 || !fd) {
    return 0;
  }
  struct file *cur_file = (thread_current()->fileArray)[fd];
  unsigned result = (unsigned) file_tell(cur_file);
  return result;
}

/* Syscall close: closes given file descriptor */
void
close (int fd)
{
  if (fd < 2 || fd > 128 || !fd) {
    exit(-1);
  }
  struct file *cur_file = (thread_current()->fileArray)[fd];
  (thread_current()->fileArray)[fd] = NULL;
  thread_current()->num_fds -= 1;
  file_close(cur_file);
}

// Method called filesys_create2 (filesys_create is the old create tool that is now
// deprecated, and this create allows for creation of directories through is_dir bool)
bool set_dir_entry(const char *path, unsigned initial_size, bool is_dir){
    bool status = false;
    status = filesys_create(path, initial_size, is_dir);
    return status;
}

// CD's into the passed in directory, ensuring proper return of flase if 
// the passed in char *does not refer to any file, and then if it does not 
// refer to a directory. Properly opens the new directory, and sets the cwd to it, 
// ensuring that we close the previous working directory. 
bool chdir (const char *dir) {
  struct file* file = filesys_open(dir);
  struct dir* new_dir;
  struct dir* old_dir;

  if (!file) {
      return false;
  }
  new_dir = dir_reopen((struct dir*)file);
  file_close(file);

  if(!dir) {
      return false;
  }
  old_dir = thread_current()->working_dir;
  thread_current()->working_dir = new_dir;
  dir_close(old_dir);
  return true;
}

// Tool used to create directories 
bool mkdir (const char *dir) {
  bool success = set_dir_entry(dir, 0, true);
  return success;
}

// Ensures that the fd is valid, that it represents a directory, and then
// proceeds to call dir_readdir to read entries from the directory, ensuring 
// that we don't read '.' or '..' as valid entries, returning true as long as some other
// entry exists. 
bool readdir (int fd, char *name) {
  bool status = false;
  if (fd >= 0 && fd < 128) {
      struct file *cur_file = thread_current()->fileArray[fd];
      if (dir_or_no(cur_file)) {
        if ((strcmp(name, ".") != 0 || !strcmp(name, "..") != 0)) {
          status = dir_readdir((struct dir *) (cur_file), name);

          while ((strcmp(name, ".") == 0 || strcmp(name, "..") == 0)) {
            status = dir_readdir((struct dir *) (cur_file), name);
            if (!status) {
              return false;
            }
          }
          if (!status) {
            return false;
          }
        }
     }
  }
  return status;
}

// Calls dir_or_no on a valid file descriptior to check to see if the file represents
// a directory or not. dir_or_no simply allows us to access the private boolean is_dir. 
bool isdir(int fd) {
  bool status = false;
  if (fd >= 0 && fd < 128) {
      struct file *cur_file = thread_current()->fileArray[fd];
      status = dir_or_no(cur_file);
  }
  return status;
}

// Calls filesys_get_inumber on a valid fd, otherwise returns -1. 
int inumber(int fd) {
  int inumber = -1;
  if (fd >= 0 && fd < 128) {
      struct file *cur_file = thread_current()->fileArray[fd];
      inumber = filesys_get_inumber(cur_file);
  }
  return inumber;
}



