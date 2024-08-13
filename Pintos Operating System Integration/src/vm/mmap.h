#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h> 
#include "vm/page.h"
#include "userprog/syscall.h"

/* Struct for mmap handling
* Contains map id, file memory was mapped from
* mapped address, and number of bytes read
* Pages indicate how many consecutive virtual addresses
* are associated with mmap_struct
*/
struct mmap_struct {
    mapid_t m_id;
    struct file *file;
    size_t read_bytes;
    void *start_addr;
    struct hash_elem mmap_elem;
    size_t pages;
}; 

/* Initializes memory maps */
void init_mmap_table(struct hash *m_table);

/* Creates and inserts mmap_struct into current thread's mmap table*/
int mmap_insert(mapid_t m_id, size_t read_bytes, void *vaddr, struct file *file, size_t num_pages);

/* Returns mmap_struct from current thread's mmap table given mmap_id */
struct mmap_struct *mid_get_mmap(mapid_t m_id);

/* Frees all of pages associated with the memory */
void mmap_remove_mapping(struct mmap_struct *mmap);

#endif