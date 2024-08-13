#ifndef VM_SWAP
#define VM_SWAP

struct swap_info {
	/* Similar to file_num */
	int swap_ind; 
	/* For concurrency and freeing structs */
	bool in_use; 
};


void init_swap(void); 

/* Puts in address into swap_info */
struct swap_info *swap_insert(void *vaddr); 
/* Copies swap to argued address (swap out) */
int swap_out(struct swap_info *swap, void *dest); 
/* Marks swap as possible, things can be swapped in */
void can_swap(struct swap_info *swap); 

#endif