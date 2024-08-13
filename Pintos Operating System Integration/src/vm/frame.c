#include "userprog/process.h"
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include "userprog/pagedir.h"
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
#include "frame.h"
#include "page.h"
#include "swap.h"

/* Keeps track of all of frames */
struct list frame_table;

struct frame *evict_policy(void);

/* 
* Initializes global frame table and frame_lock 
* Will be called in init.c
*/
void init_frame_table(void) {
    list_init(&frame_table);
    lock_init(&frame_lock);
}

/* Eviction policy implments a second-chance algorithm that ensures we select the LRU 
page/frame by considering every option twice. This is valid as we push every option to 
the back of the queue, so the 2nd time its reconsidered (if so), then it means that
we have waited on that option for the entire length of the queue which is the longest
possible time, and it is the LRU option. Additional considerations are made for frames
that have been pinned. */
struct frame *evict_policy() {
    while(true) {
	    struct list_elem* cur_elem = list_pop_front(&frame_table);
        struct frame *cur_frame = list_entry(cur_elem, struct frame, frame_elem);
        struct page_info *cur_page = cur_frame->page;
        cur_page->fr = cur_frame;
        if (pagedir_is_accessed(cur_page->pd, cur_frame->phys_addr) || pagedir_is_accessed(cur_page->pd, cur_page->vaddr) || cur_frame->evicting || cur_frame->pinned) {
            pagedir_set_accessed(cur_page->pd, cur_frame->phys_addr, false);
            pagedir_set_accessed(cur_page->pd, cur_page->vaddr, false);
            cur_frame->evicting = true;
            list_push_back(&frame_table, cur_elem);
        } else {
            // Otherwise the frame has already been removed so return it
            if (cur_page->type != FRAME_TABLE) {
                return cur_frame;
            }
            // Frame table arguments are on the stack and we don't want to evict those pages. 
            else {
                list_push_back(&frame_table, cur_elem);
            }
        }
    }
}

/* "Frees" frame by setting the in_use field to false */
void frame_free(struct frame *frame_freed) {
    frame_freed->in_use = false;
}

/* Evicts the page returned by evict_policy(). Writes to a swap block
if necessary or writes out back to the file if appropriate. Calls frame_free 
on the frame to allow it to be considered for a call to get_free_frame()*/
struct frame *frame_evict(struct frame *f) {
    f->evicting = true;
	struct page_info *page = f->page;
    ASSERT(page->fr == f);
    ASSERT(f->evicting);
	switch (page->type) {
		case FILEIO :
	        if (pagedir_is_dirty(page->pd, page->vaddr)) {
				file_write_at(page->f, page->fr->phys_addr, 
                           page->write_bytes, page->offset);
                pagedir_set_dirty(page->pd, page->vaddr, false);
		    }
			break;
		default:
            struct swap_info *temp;
            temp = swap_insert(page->fr->phys_addr);
            if (!temp) {
                PANIC ("Error evicting frame.\n");
                break;
            }
			page->swap = temp;
	}
    
    frame_free(page->fr);
    return f;
}

/* 
* Gets a free frame. Chooses a frame by
* 1. Seeing if any frames are free. If so, return that frame
* 2. If all existing frames are being used, get new frame by calling
*    palloc_get_page. If success, initialize new frame and return
* 3. If palloc_get_page failed, ran out of memory. Evict a frame and return
*    return that frame
*/
struct frame *get_free_frame(void) {
    lock_acquire(&frame_lock);

    struct frame *free_frame = NULL;
    struct list_elem *cur_elem;

    /* 
    * Loops through the frame table to see if there are 
    * any free frames
    */
    if (!list_empty(&frame_table)) {
        for (cur_elem = list_begin(&frame_table); cur_elem != list_end(&frame_table); cur_elem = list_next(cur_elem)) {
            struct frame *cur_frame = list_entry(cur_elem, struct frame, frame_elem);
            if (!(cur_frame->in_use)) {
                free_frame = cur_frame;
                break;
            }
        }
    }

    if (free_frame != NULL) {
        free_frame->in_use = true;
        pagedir_set_dirty(thread_current()->pagedir, free_frame->page, false);
        lock_release(&frame_lock);
        return free_frame;
    }

    /* No free frames found, try to allocate a new one */
    else {
        uint8_t *kpage = palloc_get_page(PAL_USER|PAL_ZERO);
        if (kpage == NULL) {
            /* Try to evict a frame according to algorithm */
            struct frame *evicted = evict_policy();
            frame_evict(evicted);
            pagedir_set_dirty(thread_current()->pagedir, evicted, false);
            (evicted)->evicting = false;
            (evicted)->in_use = true;
            (evicted)->pinned = false;
            lock_release(&frame_lock);
            return evicted;
        }
        struct frame *new_frame = (struct frame *) malloc (sizeof(struct frame));
        (new_frame)->evicting = false;
        (new_frame)->in_use = true;
        (new_frame)->phys_addr = kpage;
        (new_frame)->pinned = false;
        list_push_back(&frame_table, &((new_frame)->frame_elem));
        pagedir_set_dirty(thread_current()->pagedir, kpage, false);
        lock_release(&frame_lock);
        return new_frame;
    }
}

/* Creates a new frame, used for stack growth */
struct frame *frame_create(int flags, void* upage) {

	lock_acquire(&frame_lock);
    void *kpage = palloc_get_page(flags);
	if (!kpage) {
		return frame_evict(evict_policy());
	}

	struct frame *new_frame = (struct frame *)malloc(sizeof(struct frame));
    new_frame->evicting = false;
	new_frame->phys_addr = kpage;
    new_frame->page = NULL;
    pagedir_set_page (thread_current()->pagedir, upage, kpage, true);
    pagedir_set_dirty(thread_current()->pagedir, kpage, false);
	list_push_back(&frame_table, &new_frame->frame_elem);
    lock_release(&frame_lock);
	
	return new_frame;
}

