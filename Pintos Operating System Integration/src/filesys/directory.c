#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
    bool is_dir;                        /* Boolean flag for if it is a directory */
  };

bool
dir_add2 (struct dir *dir, const char *name, block_sector_t inode_sector, bool is_dir);

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), 0);
}

// Similar to dir_create, except now it has additional logic to add parent block_sector_ts
// if a parent was passed in. 
bool dir_create2(block_sector_t sector, size_t entry_cnt, block_sector_t parent) {
  if (!inode_create(sector, entry_cnt * sizeof(struct dir_entry), 1)) {
    return false;
  }

  struct dir *cur_dir = dir_open(inode_open(sector));
  dir_add2(cur_dir, ".", sector, true);
  if (parent) {
    dir_add2(cur_dir, "..", parent, true);
  }
  // Ensures that we close the new_dir to prevent additional unecessary opens. 
  dir_close(cur_dir);
  return true;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
  {
    dir->inode = inode;
    dir->pos = 0;
    return dir;
  }
  else
  {
    inode_close (inode);
    free (dir);
    return NULL; 
  }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
  {
    inode_close (dir->inode);
    free (dir);
  }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && strcmp (name, e.name) == 0) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  if (lookup (dir, name, &e, NULL)) {
    *inode = inode_open (e.inode_sector);
  }
  else {
    *inode = NULL;
  }

  return *inode != NULL;
}

// Exactly like dir_lookup2, except it also just sets the is_dir bool to whether or not
// the entry that was searched up was a directory or not. 
bool
dir_lookup2 (const struct dir *dir, const char *name,
            struct inode **inode, bool *is_dir) 
{
  ASSERT(dir != NULL);
  ASSERT(name != NULL);
  struct dir_entry e;
  if (lookup(dir, name, &e, NULL)) {
    *is_dir = e.is_dir;
    *inode = inode_open(e.inode_sector);
  }
  else {
    *inode = NULL;
  }
  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX) {
    return false;
  }

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) {
      if (!e.in_use) {
        break;
      }
  }

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  e.is_dir = false;
  off_t written = inode_write_at (dir->inode, &e, sizeof e, ofs);
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

// Similar to dir_add, but sets the boolean flag for the dir_entry that gets created
bool dir_add2 (struct dir *dir, const char *name, block_sector_t inode_sector, bool is_dir)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL)) {
    goto done;
  }

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  // printf("Offest before %d\n", ofs);
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) {
      if (!e.in_use) {
        break;
      }
  }

  /* Write slot. */
  e.in_use = true;
  e.is_dir = is_dir;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = (inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e);
 done:
  return success;
}


/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  struct inode *inode = NULL;
  struct dir_entry e;
  bool success = false;
  off_t ofs;

  struct dir *check;
  char check_name[NAME_MAX + 1];
  if (!lookup (dir, name, &e, &ofs)) {
    goto done;
  }

  // If you are trying to remove the cwd, then fail
  if (e.inode_sector == filesys_get_inumber(thread_current()->working_dir)) {
    goto done;
  }

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL) {
    goto done;
  }
  
  // If you are a directory then there is more checks
  if(e.is_dir){
    // If multiple process have you open, then you can't remove it
    if(inode_open_count(inode) > 2) {
      goto done;
    }
    // If can't open, then fail
    check = dir_open(inode);
    if(!check) {
      goto done;
    }
    // Ensure that the directroy doesn't just have these, to ensure '.' and '..' edge case
    while(dir_readdir(check, check_name)){
        if(strcmp(check_name, ".") && strcmp(check_name, "..")) {
            goto done;
        }  
      }
  }
  
  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) {
    goto done;
  }

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}
