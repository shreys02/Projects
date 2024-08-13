#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define NUM_DIRECT 115
#define NUM_INDIRECT 5
#define NUM_DOUBLE_INDIRECT 4
#define NUM_ENTRIES (NUM_DIRECT + NUM_INDIRECT + NUM_DOUBLE_INDIRECT)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t direct[NUM_DIRECT];  /* List of direct blocks */
    block_sector_t indirect[NUM_INDIRECT]; /* List of indirect blocks */
    block_sector_t double_indirect[NUM_DOUBLE_INDIRECT]; /* List of doubly indirect blocks */
    int is_dir;  /* Indicates if given inode is a directory or not */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock extension_lock;         /* Lock for extending the file */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */

static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  struct inode_disk *disk_inode = NULL;
  disk_inode = calloc (1, sizeof *disk_inode);
  read_block(inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);

  // If sector at given position is direct block
  if (pos < NUM_DIRECT * BLOCK_SECTOR_SIZE) {
    block_sector_t sector_num = disk_inode->direct[pos / BLOCK_SECTOR_SIZE];
    free(disk_inode);
    return sector_num;
  }

  // If sector at given position is in indirect block
  else if (pos < NUM_DIRECT * BLOCK_SECTOR_SIZE + 64 * NUM_INDIRECT * BLOCK_SECTOR_SIZE) {

    size_t num_sectors = (pos - NUM_DIRECT * BLOCK_SECTOR_SIZE) / (BLOCK_SECTOR_SIZE * 64);
    block_sector_t indirect_sector = disk_inode->indirect[num_sectors];
    block_sector_t sector_num;

    // Calculate indirect sector number and read the block into buffer
    char indirect[BLOCK_SECTOR_SIZE];
    read_block(indirect_sector, indirect, 0, BLOCK_SECTOR_SIZE);
    size_t offset = (pos - NUM_DIRECT * BLOCK_SECTOR_SIZE - num_sectors * 64 * BLOCK_SECTOR_SIZE) / (BLOCK_SECTOR_SIZE);
    
    // Find the sector address at the given offset
    memcpy(&sector_num, indirect + 4 * offset, sizeof(block_sector_t));
    free(disk_inode);
    return sector_num;
  }

  else {
    // Calculate the doubly indirect sector number
    size_t num_sectors_left = (pos / BLOCK_SECTOR_SIZE) - NUM_DIRECT - NUM_INDIRECT * 64;
    size_t num_sectors = num_sectors_left / (64 * 64);
    block_sector_t dbl_indirect_sector = disk_inode->double_indirect[num_sectors];

    char dbl_indirect[BLOCK_SECTOR_SIZE];
    read_block(dbl_indirect_sector, dbl_indirect, 0, BLOCK_SECTOR_SIZE);
    size_t offset_ind = (pos - NUM_DIRECT * BLOCK_SECTOR_SIZE - NUM_INDIRECT * 64 * BLOCK_SECTOR_SIZE) / (64 * 64 * BLOCK_SECTOR_SIZE);
    
    // Get which indirect sector offset refers to 
    block_sector_t indirect_sector;
    memcpy(&indirect_sector, dbl_indirect + offset_ind * 4, sizeof(block_sector_t));

    // Read the data at the indirect sector
    char indirect[BLOCK_SECTOR_SIZE];
    read_block(indirect_sector, indirect, 0, BLOCK_SECTOR_SIZE);
    size_t offset = (pos - NUM_DIRECT * BLOCK_SECTOR_SIZE - num_sectors * 64 * BLOCK_SECTOR_SIZE) / (BLOCK_SECTOR_SIZE);
    
    // Find the direct sector at the given offset
    block_sector_t sector_num;
    memcpy(&sector_num, indirect + 4 * offset, sizeof(block_sector_t));
    free(disk_inode);
    return sector_num;
  }
  return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, int is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      // Calculate number of sectors nucessary
      size_t sectors = bytes_to_sectors (length);
      
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir;

      static char zeros[BLOCK_SECTOR_SIZE];
      
      size_t direct_sectors;

      size_t indirect_sectors;
      size_t indirect_direct;

      size_t dbl_indirect_sectors;
      size_t dbl_ind_indirect_sectors;
      size_t dbl_direct_sectors;

      // Calculate number of direct, indirect, doubly indirect sectors needed
      if (sectors <= NUM_DIRECT) {
        direct_sectors = sectors;
        indirect_sectors = 0;
        dbl_indirect_sectors = 0;
      }
      else if (sectors <= NUM_DIRECT + NUM_INDIRECT * 64) {
        direct_sectors = NUM_DIRECT;
        indirect_sectors = DIV_ROUND_UP((sectors - NUM_DIRECT), 64);
        indirect_direct = (sectors - NUM_DIRECT) % 64;
        dbl_indirect_sectors = 0;
      }
      else if (sectors <= NUM_DIRECT + NUM_INDIRECT * 64 + NUM_DOUBLE_INDIRECT * 64 * 64) {
        direct_sectors = NUM_DIRECT;
        indirect_sectors = NUM_INDIRECT;
        indirect_direct = 64;

        dbl_indirect_sectors = DIV_ROUND_UP((sectors - NUM_DIRECT - NUM_INDIRECT * 64), (64 * 64));
        dbl_ind_indirect_sectors = (sectors - NUM_DIRECT - NUM_INDIRECT * 64) % (64 * 64);
        dbl_direct_sectors = ((sectors - NUM_DIRECT - NUM_INDIRECT * 64) % (64 * 64)) % 64;
      }

      // Allocate all of direct sectors first
      for (size_t i = 0; i < direct_sectors; i++) {
        block_sector_t new_sector = free_map_allocate_one();
        block_write (fs_device, new_sector, zeros);
        disk_inode->direct[i] = new_sector;
      }

      // Allocate all of indirect sectors next
      for (size_t i = 0; i < indirect_sectors; i++) {
        // Allocate a new block for indirect sector
        block_sector_t ind_new_sector = free_map_allocate_one();
        disk_inode->indirect[i] = ind_new_sector;
        static char direct[BLOCK_SECTOR_SIZE];

        // If last indirect sector, number of direct sectors inside
        // indirect sector is calculated according to remaining sectors
        if (i == indirect_sectors - 1) {
          for (size_t j=0; j < indirect_direct; j++) {
            block_sector_t new_sector = free_map_allocate_one();
            block_write (fs_device, new_sector, zeros);
            memcpy(direct + j * 4, &new_sector, sizeof(block_sector_t));
          }
        }
        
        // Otherwise, 64 direct sectors are required
        else {
          for (size_t j=0; j < BLOCK_SECTOR_SIZE / 8; j++) {
            block_sector_t new_sector = free_map_allocate_one();
            block_write (fs_device, new_sector, zeros);
            memcpy(direct + j * 4, &new_sector, sizeof(block_sector_t));
          }
        }
        block_write (fs_device, ind_new_sector, direct);
      }

      // Allocate doubly indirect block
      for (size_t i=0; i < dbl_indirect_sectors; i ++) {
        // New doubly indirect block: "pointers" to indirect blocks
        block_sector_t dbl_ind_new_sector = free_map_allocate_one();
        disk_inode->double_indirect[i]= dbl_ind_new_sector;
        static char indirect[BLOCK_SECTOR_SIZE];

        // Take care of last doubly indirect block
        if (i == dbl_indirect_sectors) {
          // Allocate indirect blocks inside doubly indirect block
          for (size_t j=0; j < dbl_ind_indirect_sectors; j++) {
            block_sector_t ind_new_sector = free_map_allocate_one();
            memcpy(indirect + j * 4, &ind_new_sector, sizeof(block_sector_t));
            static char direct[BLOCK_SECTOR_SIZE];

            // Take care of last indirect block: should not have all of direct blocks
            if (j == dbl_ind_indirect_sectors) {
              for (size_t k=0; k < dbl_direct_sectors; k++) {
                block_sector_t new_direct_sector = free_map_allocate_one();
                block_write (fs_device, new_direct_sector, zeros);
                memcpy(direct + k * 4, &new_direct_sector, sizeof(block_sector_t));
              }
            }
            // If not last indirect block, 64 new direct blocks should be allocated
            else {
              for (size_t k=0; k < BLOCK_SECTOR_SIZE / 8; k++) {
                block_sector_t new_direct_sector = free_map_allocate_one();
                block_write (fs_device, new_direct_sector, zeros);
                memcpy(direct + k * 4, &new_direct_sector, sizeof(block_sector_t));
              }
            }

            block_write(fs_device, ind_new_sector, direct);
          }
        }
        // Otherwise, 64 indirect blocks should be allocated, and for
        // each of the indirect blocks, 64 blocks should be allocated
        else {
          for (size_t j=0; j < BLOCK_SECTOR_SIZE / 8; j++) {
            block_sector_t ind_new_sector = free_map_allocate_one();
            memcpy(indirect + j * 4, &ind_new_sector, sizeof(block_sector_t));
            static char direct[BLOCK_SECTOR_SIZE];

            for (size_t k=0; k < BLOCK_SECTOR_SIZE / 8; k++) {
                block_sector_t new_direct_sector = free_map_allocate_one();
                block_write (fs_device, new_direct_sector, zeros);
                memcpy(direct + k * 4, &new_direct_sector, sizeof(block_sector_t));
            }

            block_write(fs_device, ind_new_sector, direct);
          }
        }
        block_write(fs_device, dbl_ind_new_sector, indirect);
      }

      disk_inode->start = sector;
      block_write (fs_device, sector, disk_inode);
      free(disk_inode);
      success = true; 
    }
  
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&(inode->extension_lock));

  cache_sector(sector);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL) {
    inode->open_cnt++;
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL) {
    return;
  }

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {

      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {

          free_map_release (inode->sector, 1); 
          struct inode_disk *disk_inode = NULL;
          disk_inode = calloc (1, sizeof *disk_inode);
          read_block(inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);

          // Free all of direct blocks
          for (size_t i=0; i < NUM_DIRECT; i++) {
            block_sector_t b = disk_inode->direct[i];
            if (b != 0) {
              free_map_release(b, 1);
            }
          }

          // Free all of indirect blocks
          for (size_t i=0; i < NUM_INDIRECT; i++) {
            block_sector_t b = disk_inode->indirect[i];
            if (b != 0) {
              static char direct[BLOCK_SECTOR_SIZE];
              block_read(fs_device, b, direct);
              for (size_t j=0; j < BLOCK_SECTOR_SIZE / 8; j++) {
                block_sector_t db;
                memcpy(db, direct + j * 4, sizeof(block_sector_t));
                if (db != 0) {
                  free_map_release(db, 1);
                }
              }
              free_map_release(b, 1);
            }
          }

          // Free all of doubly indirect blocks
          for (size_t i=0; i < NUM_DOUBLE_INDIRECT; i++) {
            block_sector_t b = disk_inode->double_indirect[i];
            if (b != 0) {
              static char db_indirect[BLOCK_SECTOR_SIZE];
              block_read(fs_device, b, db_indirect);
              for (size_t j=0; j < BLOCK_SECTOR_SIZE / 8; j++) {
                block_sector_t db;
                memcpy(db, db_indirect + j * 4, sizeof(block_sector_t));
                if (db != 0) {
                  static char indirect[BLOCK_SECTOR_SIZE];
                  block_read(fs_device, db, indirect);
                  for (size_t k=0; k < BLOCK_SECTOR_SIZE / 8; k++) {
                    block_sector_t direct;
                    memcpy(direct, indirect + k * 4, sizeof(block_sector_t));
                    free_map_release(direct, 1);
                  }
                }
              }
              free_map_release(b, 1);
            }
          }
          free(disk_inode);
        }
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{

  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      
      // Read the given block
      read_block(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

      if (size - chunk_size > 0) {
        // If we have the next block, read the next block as well
        block_sector_t sector_next = byte_to_sector (inode, offset + chunk_size);
        read_ahead(sector_next);
      }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* 
* Extended given inode by allocating right number 
* of direct, indirect, doubly indirect blocks
*/
void inode_extend(struct inode *inode, off_t new_length) {
  struct inode_disk *disk_inode = NULL;
  disk_inode = calloc (1, sizeof *disk_inode);
  read_block(inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  off_t length = new_length;

  static char zeros[BLOCK_SECTOR_SIZE];
  size_t orig_sectors = bytes_to_sectors(disk_inode->length);
  size_t new_sectors = bytes_to_sectors(new_length);

  /* Only need to create direct blocks*/
  if (new_sectors <= NUM_DIRECT) {
    for (size_t i = orig_sectors; i < new_sectors; i++) {
      block_sector_t new_sector = free_map_allocate_one();
      block_write (fs_device, new_sector, zeros);
      disk_inode->direct[i] = new_sector;
    }
  }

  /* Need to create indirect blocks, maybe need more direct blocks */
  else if (new_sectors <= NUM_DIRECT + 64 * NUM_INDIRECT) {

    // If there were less than NUM_DIRECT blocks originally,
    // allocate direct blocks first
    if (orig_sectors <= NUM_DIRECT) {
      for (size_t j=orig_sectors; j < NUM_DIRECT; j++) {
        block_sector_t new_direct_sector = free_map_allocate_one();
        block_write (fs_device, new_direct_sector, zeros);
        disk_inode->direct[j] = new_direct_sector;
      }
    }

    // Get the number of indirect sectors that previously existed
    size_t num_orig_ind_sectors; 
    if (orig_sectors > NUM_DIRECT) {
      num_orig_ind_sectors = DIV_ROUND_UP((orig_sectors - NUM_DIRECT), 64);
    }
    else {
      num_orig_ind_sectors = 0;
    }
    
    // Get new number of indirect sectors needed
    size_t num_ind_sectors = DIV_ROUND_UP((new_sectors - NUM_DIRECT), 64);

    // If the needed indirect sector already exists
    if (num_orig_ind_sectors == num_ind_sectors) {

        static char indirect_j[BLOCK_SECTOR_SIZE];
        read_block(disk_inode->indirect[num_orig_ind_sectors - 1], indirect_j, 0, BLOCK_SECTOR_SIZE);

        size_t num_direct_orig = orig_sectors - NUM_DIRECT - 64 * (num_orig_ind_sectors - 1);
        size_t num_direct_needed = new_sectors - NUM_DIRECT - 64 * (num_orig_ind_sectors - 1);

        // Obtain how many direct blocks existed inside the indirect block, and allocate new ones
        for (size_t k=num_direct_orig; k < num_direct_needed; k++) {
          block_sector_t new_direct = free_map_allocate_one();
          block_write(fs_device, new_direct, zeros);
          memcpy(indirect_j + k * 4, &new_direct, sizeof(block_sector_t));
        }

        write_block(disk_inode->indirect[num_orig_ind_sectors - 1], indirect_j, 0, BLOCK_SECTOR_SIZE);
    }

    // Otherwise, allocate new indirect blocks
    for (size_t j=num_orig_ind_sectors; j < num_ind_sectors; j++) {
      
      if (disk_inode->indirect[j] != 0) {
        static char indirect_j[BLOCK_SECTOR_SIZE];
        read_block(disk_inode->indirect[j], indirect_j, 0, BLOCK_SECTOR_SIZE);
        size_t num_direct_orig = orig_sectors - NUM_DIRECT - 64 * j;
        size_t num_direct_needed = new_sectors - NUM_DIRECT - 64 * j;

        for (size_t k=num_direct_orig; k < num_direct_needed; k++) {
          block_sector_t new_direct = free_map_allocate_one();
          block_write(fs_device, new_direct, zeros);
          memcpy(indirect_j + k * 4, &new_direct, sizeof(block_sector_t));
        }

        write_block(disk_inode->indirect[j], indirect_j, 0, BLOCK_SECTOR_SIZE);
      }

      // If indirect block does not exist, create a new one and fill it with
      // newly allocated direct blocks
      else {
        size_t num_direct_needed = new_sectors - NUM_DIRECT - 64 * j;
        block_sector_t ind_new_sector = free_map_allocate_one();
        disk_inode->indirect[j] = ind_new_sector;
        static char direct[BLOCK_SECTOR_SIZE];

        for (size_t k=0; k < num_direct_needed; k++) {
          block_sector_t new_direct = free_map_allocate_one();
          block_write(fs_device, new_direct, zeros);
          memcpy(direct + k * 4, &new_direct, sizeof(block_sector_t));
        }

        write_block(ind_new_sector, direct, 0, BLOCK_SECTOR_SIZE);
      }
    }
  }

  else {
    size_t dbl_indirect_sectors = DIV_ROUND_UP((new_sectors - NUM_DIRECT - NUM_INDIRECT * 64), 64);
    size_t dbl_ind_indirect_sectors = DIV_ROUND_UP((new_sectors - NUM_DIRECT - NUM_INDIRECT * 64), 64 * 64);
    size_t dbl_direct_sectors = DIV_ROUND_UP((new_sectors - NUM_DIRECT - NUM_INDIRECT * 64), 64 * 64 * 64);

    for (size_t i=0; i < dbl_indirect_sectors; i ++) {
      // New doubly indirect block: "pointers" to indirect blocks
      block_sector_t dbl_ind_new_sector = free_map_allocate_one();
      disk_inode->double_indirect[i]= dbl_ind_new_sector;
      static char indirect[BLOCK_SECTOR_SIZE];

      // Take care of last doubly indirect block
      if (i == dbl_indirect_sectors) {
        // Allocate indirect blocks inside doubly indirect block
        for (size_t j=0; j < dbl_ind_indirect_sectors; j++) {
          block_sector_t ind_new_sector = free_map_allocate_one();
          memcpy(indirect + j * 4, &ind_new_sector, sizeof(block_sector_t));
          static char direct[BLOCK_SECTOR_SIZE];

          // Take care of last indirect block: should not have all of direct blocks
          if (j == dbl_ind_indirect_sectors) {
            for (size_t k=0; k < dbl_direct_sectors; k++) {
              block_sector_t new_direct_sector = free_map_allocate_one();
              block_write (fs_device, new_direct_sector, zeros);
              memcpy(direct + k * 4, &new_direct_sector, sizeof(block_sector_t));
            }
          }
          // If not last indirect block, 64 new direct blocks should be allocated
          else {
            for (size_t k=0; k < BLOCK_SECTOR_SIZE / 8; k++) {
              block_sector_t new_direct_sector = free_map_allocate_one();
              block_write (fs_device, new_direct_sector, zeros);
              memcpy(direct + k * 4, &new_direct_sector, sizeof(block_sector_t));
            }
          }

          block_write(fs_device, ind_new_sector, direct);
        }
      }
      // Otherwise, 64 indirect blocks should be allocated, and for
      // each of the indirect blocks, 64 blocks should be allocated
      else {
        for (size_t j=0; j < BLOCK_SECTOR_SIZE / 8; j++) {
          block_sector_t ind_new_sector = free_map_allocate_one();
          memcpy(indirect + j * 4, &ind_new_sector, sizeof(block_sector_t));
          static char direct[BLOCK_SECTOR_SIZE];

          for (size_t k=0; k < BLOCK_SECTOR_SIZE / 8; k++) {
              block_sector_t new_direct_sector = free_map_allocate_one();
              block_write (fs_device, new_direct_sector, zeros);
              memcpy(direct + k * 4, &new_direct_sector, sizeof(block_sector_t));
          }

          block_write(fs_device, ind_new_sector, direct);
        }
      }
      block_write(fs_device, dbl_ind_new_sector, indirect);
    }
  }

  disk_inode->length = new_length;
  write_block(inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  free(disk_inode);
  return length;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt) {
    return 0;
  }

  size_t length = inode_length(inode);
  barrier();
  if (length < offset + size) {
    lock_acquire(&(inode->extension_lock));
    if (inode_length(inode) < offset + size) {
      inode_extend(inode, offset + size);
    }
    lock_release(&(inode->extension_lock));
  }

  while (size > 0) 
    { 
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      write_block(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

int
/* Returns deny_write count for given inode */
inode_get_write (struct inode *inode) 
{
  return inode->deny_write_cnt;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk *disk_inode = NULL;
  disk_inode = calloc (1, sizeof *disk_inode);
  read_block(inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  off_t length = disk_inode->length;
  free(disk_inode);
  return length;
}

/* Returns the starting block for given inode */
block_sector_t
inode_start (const struct inode *inode)
{
  struct inode_disk *disk_inode = NULL;
  disk_inode = calloc (1, sizeof *disk_inode);
  read_block(inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  block_sector_t start = disk_inode->start;
  free(disk_inode);
  return start;
}

int
inode_deny_write_count (const struct inode *inode)
{
  return inode->deny_write_cnt;
}

/* Returns open_count on inode */ 
int inode_open_count(struct inode *inode) {
  return inode->open_cnt;
}
