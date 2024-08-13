#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "frame.h"
#include "page.h"

bool less_func(const struct hash_elem *a, const struct hash_elem *b, void* aux UNUSED);
unsigned hash_func(const struct hash_elem *e, void *aux UNUSED);
bool valid_data(struct hash *h_table, void *vaddr);

// Implemented necessary hash table functionality
unsigned hash_func(const struct hash_elem *e, void *aux UNUSED) {
    return hash_int((int) hash_entry(e, struct page_info, elem)->vaddr);
}
bool less_func(const struct hash_elem *a, const struct hash_elem *b, void* aux UNUSED) {
    return hash_entry(a, struct page_info, elem)->vaddr < hash_entry(b, struct page_info, elem)->vaddr;
}

void free_func(struct hash_elem *elem, void *aux UNUSED) {
    struct page_info *page = hash_entry(elem, struct page_info, elem);
    if (page->fr) {
        frame_free(page->fr);
    }
    pagedir_clear_page(thread_current()->pagedir, page->vaddr);
    free(page);
}

/* 
* Initializes the supplemental page table 
* Called for individual threads
*/
void init_page_table(struct hash *h_table) {
    hash_init(h_table, &hash_func, &less_func, NULL /* aux is NULL */);
}

/*
* Gets page_info struct associated with given virtual address
* Called with struct has *h_table, which is hash table of the
* current thread 
* example usage: get_page(thread_current()->page_table, 0x84000000)
*/
struct page_info* get_page(struct hash *h_table, void* vaddr) {
    // Ensures that it is a valid address
    if (!is_user_vaddr(vaddr)) {
        return NULL;
    }
    // All addresses have to be page aligned
    if (pg_ofs(vaddr) != 0) {
        return NULL;
    }

    struct page_info cur_page;
    cur_page.vaddr = vaddr;
    struct hash_elem *cur_elem;

    // Hashes into the hash table to find the element with given address
    cur_elem = hash_find(h_table, &cur_page.elem);
    if (cur_elem) {
        return hash_entry(cur_elem, struct page_info, elem);
    }
    else {
        return NULL;
    }
}   

bool valid_data(struct hash *h_table, void *vaddr) {
    return get_page(h_table, vaddr) != NULL;
}

/* 
* Sets frame to page_info, and page_info to frame
*/
bool set_frame(struct page_info *cur_pg, struct frame *new_frame) {
    ASSERT(!cur_pg->fr || cur_pg->fr->evicting);
    
    new_frame->page = cur_pg;
    cur_pg->fr = new_frame;
		
	/* Mapping vaddr to the physical address */
	bool valid = pagedir_set_page(cur_pg->pd, cur_pg->vaddr, new_frame->phys_addr, cur_pg->can_write);
	return valid;
}

/* 
* Frees given page by removing it from the page table
* and freeing the page_info struct
*/
int free_page(struct hash *h_table, struct page_info *cur_pg) {
    if (!valid_data(h_table, cur_pg->vaddr)) {
        return 0;
    }
    // Removes element from the hash table
    hash_delete(h_table, &cur_pg->elem);
    // Frees the page_info struct, which would have been malloced
    free((void*)cur_pg);
	return 1;
}

/* 
* Destroys given hash table, and deletes all of
* mappings from pages to frames
*/
void free_page_table(struct hash *h_table) {
    hash_apply(h_table, &free_func);
    hash_destroy(h_table, NULL);
}

/* 
* Pins a page so that it cannot be evicted
*/
void pin_page(struct hash *h_table, void *vaddr){
	void *vpage = pg_round_down(vaddr);
	struct page_info *page = get_page(h_table, vpage);
	page->fr->pinned = true;
}

/* 
* Unpins the page after eviction
*/
void unpin_page(struct hash *h_table, void *vaddr){
	void *vpage = pg_round_down(vaddr);
	struct page_info *page = get_page(h_table, vpage);
	page->fr->pinned = false;
}

