#include "filesys/free-map.h"
#include <debug.h>
#include "filesys/inode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/timer.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/process.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "cache.h"

struct list cache_list;
/* Initializes the cache_list, called in init.c */
void cache_init() {
    list_init(&cache_list);
}

/* 
* Obtains cache_info struct for given sector. Returns NULL if
* the sector has not been cached 
*/
struct cache_info *get_info(block_sector_t sector) {
    struct cache_info *curr_cache = NULL;
    struct list_elem *cur_elem;
    struct list_elem *remove_elem;
    /* 
    * Loops through the cache_list to see if sector is stored
    * in the cache
    */
    if (!list_empty(&cache_list)) {
        for (cur_elem = list_begin(&cache_list); cur_elem != list_end(&cache_list); cur_elem = list_next(cur_elem)) {
            struct cache_info *cur_info = list_entry(cur_elem, struct cache_info, cache_elem);
            if (cur_info->sector == sector) {
                curr_cache = cur_info;
                remove_elem = cur_elem;
                break;
            }
        }
        /* 
        * If sector has been cached, then remove it from its current position
        * and push it to the back. This ensures that the element at the front
        * has been accessed least recently
        */
        if (curr_cache != NULL) {
            list_remove(remove_elem);
            list_push_back(&cache_list, remove_elem);
        }
    }
    return curr_cache;
}

/* Caches a given sector, usually called for inode_disk*/
void cache_sector(block_sector_t sector) {
    /* If cache does not already exist, create new one */
    if (get_info(sector) == NULL) {

        struct cache_info *new_info = malloc(sizeof(struct cache_info));

        new_info->sector = sector;
        new_info->accessed = true;
        new_info->is_dirty = false;
        new_info->recent_accesses = 1;
        lock_init(&(new_info->rw_lock));

        lock_acquire(&(new_info->rw_lock));;
        block_read(fs_device, sector, new_info->cache_data);
        lock_release(&(new_info->rw_lock));

        if (list_size(&cache_list) == 64) {
            evict_cache();
        }

        list_push_back(&cache_list, &(new_info->cache_elem));
    }
}

/* 
* Reads given buffer into cache
* If cache does not exist for given sector, create new one
*/
void read_block(block_sector_t sector, char *buffer, int sector_ofs, off_t size) {
    // See if sector is already in cache_list
    struct cache_info *info = get_info(sector);

    // If cache exists, simply write from buffer into cache data field
    if (info != NULL) {
        info->recent_accesses += 1;
        info->accessed = true;
        lock_acquire(&(info->rw_lock));
        memcpy(buffer, info->cache_data + sector_ofs, size);
        lock_release(&(info->rw_lock));
    }

    // Otherwise, create new cache and add it to the list
    else {
        struct cache_info *new_info = malloc(sizeof(struct cache_info));
        new_info->sector = sector;
        new_info->accessed = true;
        new_info->is_dirty = false;
        new_info->recent_accesses = 1;

        lock_init(&(new_info->rw_lock));

        lock_acquire(&(new_info->rw_lock));
        block_read(fs_device, sector, new_info->cache_data);
        memcpy(buffer, new_info->cache_data + sector_ofs, size);
        lock_release(&(new_info->rw_lock));

        if (list_size(&cache_list) == 64) {
            evict_cache();
        }

        list_push_back(&cache_list, &(new_info->cache_elem));
    }
}

/* 
* Function implemented for read ahead
* Should only be called when you can be certain you are going to
* be reading the next block
*/
void read_ahead(block_sector_t sector) {
    // See if sector is already in cache_list
    struct cache_info *info = get_info(sector);

    // If cache exists, simply write from buffer into cache data field
    // Otherwise, create new cache and add it to the list
    if (info == NULL) {
        struct cache_info *new_info = malloc(sizeof(struct cache_info));
        new_info->sector = sector;
        new_info->accessed = true;
        new_info->is_dirty = false;
        new_info->recent_accesses = 1;

        lock_init(&(new_info->rw_lock));
        block_read(fs_device, sector, new_info->cache_data);

        if (list_size(&cache_list) == 64) {
            evict_cache();
        }

        list_push_back(&cache_list, &(new_info->cache_elem));
    }
}

/* 
* Writes from cache into given buffer
* If cache does not exist for given sector, create new one
*/
void write_block(block_sector_t sector, char *buffer, int sector_ofs, off_t size) {
    // Check if the sector already exists in the cached list
    struct cache_info *info = get_info(sector);
    
    // If it exists, simply write to the buffer of the cache_entry
    if (info != NULL) {
        info->is_dirty = true;
        info->recent_accesses += 1;
        info->accessed = true;
        lock_acquire(&(info->rw_lock));
        memcpy(info->cache_data + sector_ofs, buffer, size);
        lock_release(&(info->rw_lock));
    }
    
    // Otherwise, create a new cache_entry
    else {
        struct cache_info *new_info = malloc(sizeof(struct cache_info));
        new_info->sector = sector;
        new_info->accessed = true;
        new_info->is_dirty = true;
        new_info->recent_accesses = 1;
        lock_init(&(new_info->rw_lock));
        block_read(fs_device, sector, new_info->cache_data);
        memcpy(new_info->cache_data + sector_ofs, buffer, size);
        if (list_size(&cache_list) == 64) {
            evict_cache();
        }
        list_push_back(&cache_list, &(new_info->cache_elem));
    }
}

/* 
* Evicts a given cache
* Write data in the cache entry back into the disk
* Removes the cache entry from the cache list
* Should be called when the size of cache list is greater than 64
*/
void evict_cache(void) {
    struct list_elem *evicted = list_front(&cache_list);
    list_remove(evicted);
    struct cache_info *cur_info = list_entry(evicted, struct cache_info, cache_elem);
    lock_acquire(&(cur_info->rw_lock));
    block_write (fs_device, cur_info->sector, cur_info->cache_data);
    lock_release(&(cur_info->rw_lock));
    free(cur_info);
}

/* 
* A function that gets called periodically to "flush" the cache
* If the cache entry has been modified since it has been created,
* write the data at the cache entry back into the disk
*/
void refresh_cache(void) {
    struct cache_info *curr_cache = NULL;
    struct list_elem *cur_elem;
    if (!list_empty(&cache_list)) {
        for (cur_elem = list_begin(&cache_list); cur_elem != list_end(&cache_list); cur_elem = list_next(cur_elem)) {
            struct cache_info *cur_info = list_entry(cur_elem, struct cache_info, cache_elem);
            if (cur_info->is_dirty) {
                lock_acquire(&(cur_info->rw_lock));
                block_write (fs_device, cur_info->sector, cur_info->cache_data);
                lock_release(&(cur_info->rw_lock));
                cur_info->is_dirty = false;
            }
        }
    }
}

/* 
* A function that gets called at the very end to write data back
* into the disk
*/
void delete_cache(void) {
    struct cache_info *curr_cache = NULL;
    struct list_elem *cur_elem = list_begin(&cache_list);

    if (!list_empty(&cache_list)) {
        while (cur_elem != list_end(&cache_list)) {
            struct cache_info *cur_info = list_entry(cur_elem, struct cache_info, cache_elem);
            if (cur_info->is_dirty) {
                lock_acquire(&(cur_info->rw_lock));
                block_write (fs_device, cur_info->sector, cur_info->cache_data);
                lock_release(&(cur_info->rw_lock));
            }
            cur_elem = list_remove(cur_elem);
            free(cur_info);
        }
        list_init(&cache_list);
    }
}
