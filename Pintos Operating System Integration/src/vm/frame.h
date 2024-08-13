#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h> 
#include "vm/page.h"

/* Avoids race conditions between multiple processes */
static struct lock frame_lock;

/* Struct for handling frames
* Includes the physical address of the frame,
* whether it should be pinned and if it is being used
* Additionally stores supplemental information that will
* be used by the page handler.
*/
struct frame {
    void *phys_addr; // Phys_addr associated
    bool pinned; // Whether frame is pinned or not
    bool evicting; // In process of being evicted
    bool in_use; // Currently being used or not
    struct page_info *page; // Page associated
    struct list_elem frame_elem;
}; 

/* Initializes the frame table as well as the 
* global lock for accessing frames to ensure proper
synchronization. 
*/
void init_frame_table(void); 

/* Checks the frame table to see if there are
* any frames that are not in use. If yes, returns the
* address. Otherwise, creates a new frame, gets a new page,
* and adds the new frame to frame table
*/
struct frame *get_free_frame(void);

/* This function evicts a frame f chosen by the eviction policy helper
method, properly swapping out its information if necessary or writing
out its information to its corresponding file if possible. Calls frame_free
to allow for proper reconsideration. */
struct frame *frame_evict(struct frame *f);

/* "Frees" a frame by setting frame's in_use flag to false. This allows 
it to be chosen by get_free_frame again.  */
void frame_free(struct frame *frame_freed); 

/* This function is used for stack growth and forces an eviction
to occur to ensure there is more space for the new piece of information, 
correctly placing the information already present at the page into a 
swap. Returns the newly created frame to ensure we can set the associated
field within its corresponding page.  */
struct frame *frame_create(int flags, void* upage);
#endif