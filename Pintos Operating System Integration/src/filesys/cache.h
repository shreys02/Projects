#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include "devices/block.h"
#include "threads/synch.h"

struct cache_info {
    struct list_elem cache_elem; 

    block_sector_t sector; 
    char cache_data[BLOCK_SECTOR_SIZE]; 

    bool accessed; // True if accessed since last check
    int recent_accesses; // For checking how recently this was accessed
    bool is_dirty; // True if block has been written to since last check
    struct lock rw_lock; // Read-write lock for synchronization
};

/* Initializes global buffer cache which stores cache_infos. */
void cache_init(void); 

// Caches a sector when it is first created, for inode_disk 
void cache_sector(block_sector_t sector);

// Reads block into cache and returns it.
void read_block(block_sector_t sector, char *buffer, int sector_ofs, off_t size); 

void read_ahead(block_sector_t sector);

/* Writes to block from cache. */
void write_block(block_sector_t sector, char *buffer, int sector_ofs, off_t size); 

/* Writes all dirty blocks in cache to memory, called periodically. */
void refresh_cache(void); 

// Writes all dirty blocks in cache to memory when file system shuts down */
void delete_cache(void);

#endif // #ifndef BUFFER_CACHE