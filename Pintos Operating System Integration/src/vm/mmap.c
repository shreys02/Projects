#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <round.h>

#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/process.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/mmap.h"

bool mmap_less_func(const struct hash_elem *a, const struct hash_elem *b, void* aux UNUSED);
unsigned mmap_hash_func(const struct hash_elem *e, void *aux UNUSED);

// Necessary functions for hash table functionality
unsigned mmap_hash_func(const struct hash_elem *e, void *aux UNUSED) {
    return hash_int((int) hash_entry(e, struct mmap_struct, mmap_elem)->m_id);
}
bool mmap_less_func(const struct hash_elem *a, const struct hash_elem *b, void* aux UNUSED) {
    return hash_entry(a, struct mmap_struct, mmap_elem)->m_id < hash_entry(b, struct mmap_struct, mmap_elem)->m_id;
}

/* Initializes memory maps, should be called for every thread */
void init_mmap_table(struct hash *m_table) {
    hash_init(m_table, &mmap_hash_func, &mmap_less_func, NULL /* aux is NULL */);
}

/* Creates and inserts mmap_struct into current thread's mmap table*/
int mmap_insert(mapid_t m_id, size_t read_bytes, void *vaddr, struct file *file, size_t num_pages) {
    // Create mmap_struct and set all of necessary fields
    struct mmap_struct *elem = (struct mmap_struct *) malloc(sizeof(struct mmap_struct));
    elem->m_id = m_id;
    elem->file = file;
    elem->read_bytes = read_bytes;
    elem->start_addr = vaddr;
    elem->pages = num_pages;

    // Inserts into current thread's mmap_table
    hash_insert(&(thread_current()->mmap_table), &(elem->mmap_elem));

    thread_current()->mmap_id++;
    return 1;
}

/* Returns mmap_struct from current thread's mmap table given mmap_id */
struct mmap_struct *mid_get_mmap(mapid_t m_id) {
    // Makes sure m_id is a valid map_id
    if (m_id > thread_current()->mmap_id) {
        return NULL;
    }

    struct mmap_struct cur_mmap;
    cur_mmap.m_id = m_id;
    struct hash_elem *cur_elem;
    // Obtains mmap_struct given map_id
    cur_elem = hash_find(&(thread_current()->mmap_table), &cur_mmap.mmap_elem);
    if (cur_elem) {
        return hash_entry(cur_elem, struct mmap_struct, mmap_elem);
    }
    else {
        return NULL;
    }
}

/* 
* Removes mmap_struct from the mmap table and frees all of
* pages used by the memory mapping
*/
void mmap_remove_mapping(struct mmap_struct *mmap) {
    mapid_t m_id = mmap->m_id;
    hash_delete(&(thread_current()->mmap_table), &(mmap->mmap_elem));

    void *addr = mmap->start_addr;

    for (size_t i=0; i < mmap->pages; i++) {
        struct page_info *cur_page = get_page(&(thread_current()->page_table), addr);
        free_page(&(thread_current()->page_table), cur_page);
        pagedir_clear_page(thread_current()->pagedir, addr);
        addr += PGSIZE;
    }
}