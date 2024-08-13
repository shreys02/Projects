#ifndef VM_PAGE
#define VM_PAGE

#include "frame.h"
#include "threads/vaddr.h"
#include <list.h>
#include <hash.h>

enum type {
	FILEIO, // For cases that were a result of Fileio (executables, reading, writing, etc)
	SWAP_TABLE, // For pages that refer to items that have been swapped into the swap_table
	FRAME_TABLE, // In frame_table, default case used for stack arguments as well
	MMAP // For mmap cases
};

struct page_info {
	uint32_t *pd; // Pagedir associated
	void *vaddr; // Virtual Address referenced
    struct frame *fr; // Frame associated
	struct hash_elem elem; 
	bool can_write; // Memory being writable?
	struct file *f; // File associated for fileio case, null otherwise
	enum type type;
	size_t read_bytes; // Amount to read intialized in load_segment
	int offset; // Offset based in load_segment
	int write_bytes; // Amount to write intialized in load_segment

    struct swap_info *swap; // Swap info related for swap_table case, null otherwise	
};

/* Initializes supplemental page table */
void init_page_table(struct hash *h_table); 

/* Takes in an virtual address, and finds the respective page stored inside
h_table, returns NULL if it doesn't exist.  */
struct page_info* get_page(struct hash *h_table, void* vaddr);

/* Sets the pointers between the provided page and frame, and then calls
pagedir_set_page to formalize the connection, returns a boolean to indicate
success or failure. */
bool set_frame(struct page_info *cur_pg, struct frame *new_frame); 

/* Frees the given page's informaiton, and deletes it from the hash_table. */
int free_page(struct hash *h_table, struct page_info *cur_pg); 
/* Frees/Destroys the entire hash_table */
void free_page_table(struct hash *h_table);

/* Pin page methods to handle setting the pinned field considered during eviction. */
void pin_page(struct hash *h_table, void *vaddr);
void unpin_page(struct hash *h_table, void *vaddr);

#endif