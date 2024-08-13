#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <round.h>

#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "frame.h"
#include "page.h"
#include "swap.h"

// Swap info array pointer contain indexes that relate to block_sector_t
static struct swap_info *swap_slots;
static struct block *swap_table;

block_sector_t block_sector(int swap_ind);
struct swap_info *find_empty(void); 

/* Returns the location of the block_sector that relates to the provided
swap index. */
block_sector_t block_sector(int swap_ind) {
    return (PGSIZE * swap_ind) / BLOCK_SECTOR_SIZE;
}

/* Goes through all possible number of swap_infos created, and finds the first
empty swap field, otherwise returns NULL. */
struct swap_info *swap_find_empty(void) {
	// Total possible amount of iterations
	int num_swaps = block_size(swap_table) * BLOCK_SECTOR_SIZE / PGSIZE;
	
	for (int i = 0; i < num_swaps; i++) {
		/* Attribute in_use false if the slot is free. */
		if (!swap_slots[i].in_use) {
			swap_slots[i].in_use = true;
            return &swap_slots[i];
        }
	}	
	return NULL;
}

/* Initializes the swap_table, as well as the list of swap_infos to match
the given size of the swap_table, and sets teh in_use fields to false. */
void init_swap(void) {
	swap_table = block_get_role(BLOCK_SWAP);
	
	/* One entry in the array for every page in the swap block. */
	int num_slots = block_size(swap_table) * BLOCK_SECTOR_SIZE / PGSIZE;
	swap_slots = (struct swap_slot *)malloc(num_slots * sizeof(struct swap_info));
	
    /* Each element in the array points to the next entry in the swap table, 
       separated by PGSIZE. */
	for (int i = 0; i < num_slots; i++) {
        swap_slots[i].in_use = false;
		swap_slots[i].swap_ind = i;
	}
}

/* Allows for the given swap_info to be swapped into. */
void can_swap(struct swap_info *swap) {
    // ASSERT (swap != NULL);
    if (!swap) {
        return;
    }
	swap->in_use = false;
}

/* Inserts the provided virtual address into the first found free swap_info.
Returns the swap_info that the data is now in.  */
struct swap_info *swap_insert(void *vaddr) {
	struct swap_info *swap_page = swap_find_empty();
    if (swap_page == NULL) {
        return NULL;
    }
    // ASSERT(swap_page);
    for (int i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++) {
        block_write(swap_table, block_sector(swap_page->swap_ind) + i, vaddr + BLOCK_SECTOR_SIZE * i);
    }
	return swap_page;
}

/* Swaps the data stored at the swap out into the destination address, and
allows the swap_info to be swapped into again. Returns 1 on success, 0 on failure. */
int swap_out(struct swap_info *swap, void *dest) {
	if (!swap) {
        return 0;
    }

    for (int i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++) {
        block_read(swap_table, block_sector(swap->swap_ind) + i, dest + BLOCK_SECTOR_SIZE * i);
    }

	can_swap(swap);

	return 1;
}















