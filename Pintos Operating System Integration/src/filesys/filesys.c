#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  // printf("filesys_done getting called\n");
  delete_cache();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create_file (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir;
  // Added checks to work with the current working directory if possible
  // This should always be set because process_execute should get called first
  // However, this null check is added to ensure that the persistence tests work
  // as needed, as this method gets called in fs_util before process_execute. 
  if (thread_current()->working_dir == NULL) {
    dir = dir_open_root();
  }
  else {
    dir = dir_reopen(thread_current()->working_dir);
  }
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, 0)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

// Parses the given path to get the name of the file inside the innermost directory
// and places its name inside the name file, along with setting dir and opening it. 
static bool build_path(const char* path, struct dir **dir, char *name){
  switch (*path) {
    // In case of forward slash, this refers to root, and we want to set dir to that
    case '/':
      *dir = dir_open_root();
      path++;
      // Handles edge case of path from root
      if (*path == '/') {
        path++;
      }
      break;
    // Handles case of null terminator
    case '\0':
      return false;
    // Keep moving forward if '.'
    case '.':
      if (*(path+1) == '/') {
        path++;
      } 
    // Otherwise check from the current working directory and set dir to that, if it is 
    // not set, then set it to root and build from there. 
    default:
      if (thread_current()->working_dir == NULL) {
        *dir = dir_open_root();
      }
      else {
        *dir = dir_reopen(thread_current()->working_dir);
      }
  }
  struct inode *inode;
  off_t length = 0;
  bool is_dir = false;
  while(true){
    length = 0;
    // Initial check to make sure that the length is within the NAME_MAX
    while (path[length] != '\0' && path[length] != '/'){
        length += 1;
        if(length > NAME_MAX){
            dir_close(*dir);
            return false;
        }
    } 
    // Sets name properly
    memcpy(name, path, length);
    name[length] = '\0';

    // If we need to go into another directory, use dir_lookup to 
    // validate if this is valid, setting inode and dir if so. 
    if(path[length] == '/') {
        if(!dir_lookup2(*dir, name, &inode, &is_dir)){
          dir_close(*dir);
          return false;
        }

        // If it turned out to be a file, then close the directory and return false
        if (!is_dir) {
          dir_close(*dir);
          return false;
        }
        
        // Close the current directory, and opens the one after / stored within inode. 
        dir_close(*dir);
        *dir = dir_open(inode);
        if (!*dir) {
            return false;
        }
    }
    // Handles case of '.'
    else if(path[length] == '\0'){
        if(!length) {
            memcpy(name, ".", 2);
        }
        return true;
    }
    path += length + 1;
  }
  return false;
}


int filesys_get_inumber(struct dir* dir){
    return inode_get_inumber(dir_get_inode(dir));
}

/* 
Updated filesys_create to have different behavior if it is a directory or not. 
In either situation build_path is called to ensure that the path itself makes sense, 
however, if it is not a directory, then filesys_create_file(the old filesys_create) is 
called as long as it refers to solely a file. Otherwise, there is new logic now if handling
a directory to call dir_create2, and dir_add2, which both take in additional arguments to 
properly set fields inside dir_entry. 
*/
bool filesys_create(const char *name, off_t initial_size, bool is_dir) {
  block_sector_t inode_sector = 0;
  bool success = false;
  if (!is_dir) {
    char path[NAME_MAX + 1];
    struct dir *dir = NULL;
    // If not valid immediately exit
    if(!build_path(name, &dir, path)) {
      return false;
    }
    // Checks to see if path refers solely to a file, then call filesys_create_file
    if (strcmp(path, name) == 0 || strlen(name) - strlen(path) == 1) {
      success = filesys_create_file(path, initial_size);
    }

    // Similar logic to previous conditions for success, modified to allow 
    // for directory logic. 
    else {
      success = (free_map_allocate(1, &inode_sector) &&
      (is_dir ? dir_create2(inode_sector, initial_size,
          filesys_get_inumber(dir)): 
      inode_create(inode_sector, initial_size, 0)) &&
      dir_add2(dir, path, inode_sector, false));

      if (!success && inode_sector != 0) {
        free_map_release (inode_sector, 1);
      }
      dir_close (dir);
    }
  }

  // Handle creating of a directory itself 
  else {
    char path[NAME_MAX + 1];
    struct dir *dir = NULL;
    if(!build_path(name, &dir, path)) {
      return false;
    }
    success = (free_map_allocate(1, &inode_sector) &&
    (is_dir ? dir_create2(inode_sector, initial_size,
        filesys_get_inumber(dir)): 
    inode_create(inode_sector, initial_size, 1)) &&
    dir_add2(dir, path, inode_sector, is_dir));

    if (!success && inode_sector != 0) {
      free_map_release (inode_sector, 1);
    }
    dir_close (dir);
  }

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *filesys_open(const char *name) {
  // Handles edge cases first of '/' and '.', 
  // either opening the root of the current_working_dir. 
  if (strcmp(name, "/") == 0) {
    struct dir *dir = dir_open_root();
    struct inode *new_inode = dir_get_inode(dir);
    // File_open was modified to include a boolean for whether or not its a directory
    return file_open(new_inode, true);
  }
  if (strcmp(name, ".") == 0) {
    struct dir *dir = thread_current()->working_dir;
    struct inode *new_inode = dir_get_inode(dir);
    return file_open(new_inode, true);
  }
  // Builds the path properly from name, and then properly opens it
  char path[NAME_MAX + 1];
  struct dir *dir = NULL;
  struct inode *inode = NULL;
  bool is_dir;
  if(!build_path(name, &dir, path)) {
      return NULL;
  }
  if (dir != NULL) {
      dir_lookup2(dir, path, &inode, &is_dir);
  }
  dir_close(dir);

  return file_open(inode, is_dir);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  char path[NAME_MAX + 1];
  struct dir *dir = NULL;
  if(!build_path(name, &dir, path)) {
      return false;
  }
  bool success = dir != NULL && dir_remove (dir, path);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
