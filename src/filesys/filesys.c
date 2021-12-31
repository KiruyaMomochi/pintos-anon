#include "filesys/filesys.h"
#include "filesys/cache.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "path.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>

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
  filesys_cache_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  /* Disable caching. */
  filesys_cache_disable ();

  free_map_close ();
}

/* Open the directory, whose path is first LENGTH characters
   from PATH.
   If LENGTH is 0 or NAME is null, open working directory.

   Returns the new directory if successful,
   otherwise a null pointer. */
static struct dir *
filesys_open_dir_length (const char *path, size_t length)
{
  /* If name is null or length is 0, open working directory. */
  if (length == 0 || path == NULL)
    return dir_open_current ();

  /* If name is absolute, open the root directory.
     Otherwise, open the working directory. */
  bool is_absolute = path_is_absolute (path);
  struct dir *dir = is_absolute ? dir_open_root () : dir_open_current ();

  /* Copy name for strtok. */
  char *name_copy = malloc (length + 1);
  if (name_copy == NULL)
    PANIC ("filesys: out of memory");
  strlcpy (name_copy, path, length + 1);

  char *saveptr;
  char *token;
  struct inode *inode = NULL;

  /* Iterate through the path, opening each directory. */
  for (token = strtok_r (name_copy, "/", &saveptr); token != NULL;
       token = strtok_r (NULL, "/", &saveptr))
    {
      /* Not a directory. */
      if (dir == NULL)
        break;

      /* The file is not found. */
      if (!dir_lookup (dir, token, &inode))
        goto fail;

      /* Close the current directory and open the next one. */
      dir_close (dir);
      dir = NULL;

      /* We need a directory, so if the inode is not a directory, stop. */
      if (inode_is_dir (inode))
        dir = dir_open (inode);
      else
        goto fail;
    }

  /* If we have not reached the end of the path, stop. */
  if (token != NULL)
    goto fail;

  /* Directory found, return it. */
  free (name_copy);
  return dir;

fail:

  /* Clean up and return null. */
  free (name_copy);
  if (dir != NULL)
    dir_close (dir);

  return NULL;
}

/* Creates a file at PATH with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file or directory at PATH already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size)
{
  /* Split directory into parent and base. */
  size_t parent_len;
  const char *base_begin = NULL;
  const char *base_end = NULL;
  path_split (path, &parent_len, &base_begin, &base_end);

  /* A file should not have an empty name. */
  size_t base_len = base_end - base_begin;
  if (base_len == 0)
    return false;

  /* A file should not have a trailing slash. */
  if (*base_end == PATH_SEPARATOR)
    return false;

  /* Copy base name to a new string. */
  char *base_name = NULL;
  base_name = malloc (base_len + 1);
  if (base_name == NULL)
    return false;
  strlcpy (base_name, base_begin, base_len + 1);

  /* Create base file, then add it to the parent. */
  block_sector_t inode_sector = 0;
  struct dir *parent_dir = filesys_open_dir_length (path, parent_len);
  bool success = (parent_dir != NULL && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (parent_dir, base_name, inode_sector));

  /* Clean up. */
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (parent_dir);
  free (base_name);

  return success;
}

/* Creates a new directory at PATH, which can be relative or
   absolute.
   Returns true if successful, false otherwise.
   Fails if a file or directory at PATH already exists,
   or if internal memory allocation fails. */
bool
filesys_create_dir (const char *path)
{
  /* Split directory into parent and base. */
  size_t parent_len;
  const char *base_begin = NULL;
  const char *base_end = NULL;
  path_split (path, &parent_len, &base_begin, &base_end);

  /* A directory should not have an empty name. */
  size_t base_len = base_end - base_begin;
  if (base_len == 0)
    return false;

  /* Copy base name to a new string. */
  char *base_name = NULL;
  base_name = malloc (base_len + 1);
  if (base_name == NULL)
    return false;
  strlcpy (base_name, base_begin, base_len + 1);

  /* Create base directory, then add it to the parent. */
  block_sector_t inode_sector = 0;
  struct dir *parent_dir = filesys_open_dir_length (path, parent_len);
  bool success = (parent_dir != NULL && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, 16)
                  && dir_add (parent_dir, base_name, inode_sector));

  /* Add . and .. to the new directory. */
  if (success)
    {
      struct dir *base_dir = dir_open (inode_open (inode_sector));
      success = base_dir != NULL && dir_add_dot (parent_dir, base_dir);
      dir_close (base_dir);
      if (!success)
        dir_remove (parent_dir, base_name);
    }

  /* Clean up. */
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (parent_dir);
  free (base_name);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  /* If name is null, fail. */
  if (name == NULL)
    return NULL;

  /* If name is absolute, open the root directory.
     Otherwise, open the working directory. */
  bool is_absolute = path_is_absolute (name);
  struct dir *dir = is_absolute ? dir_open_root () : dir_open_current ();

  /* A file should not have an empty name. */
  size_t name_len = strlen (name);
  if (name_len == 0)
    return false;

  /* Copy name for strtok. */
  char *name_copy = malloc (name_len + 1);
  if (name_copy == NULL)
    PANIC ("filesys: out of memory");
  strlcpy (name_copy, name, name_len + 1);

  char *saveptr;
  char *token;
  struct file *file = NULL;
  struct inode *inode = NULL;

  /* Iterate through the path, opening each directory. */
  for (token = strtok_r (name_copy, "/", &saveptr); token != NULL;
       token = strtok_r (NULL, "/", &saveptr))
    {
      /* Not a directory. */
      if (dir == NULL)
        goto fail;

      /* The file is not found. */
      if (!dir_lookup (dir, token, &inode))
        goto fail;

      /* Close last directory. */
      dir_close (dir);
      dir = NULL;

      /* If the inode is a directory, open it for the next iteration.
         Otherwise, it is a file and we are done. But we don't know
         if we have reached the end of the path, so don't break here.

         In other words: leave decision to next iteration. */
      if (inode_is_dir (inode))
        dir = dir_open (inode);
      else
        file = file_open (inode);
    }

  /* If we have not reached the end of the path, stop. */
  if (token != NULL)
    goto fail;

  /* Otherwise we found something.
     But if it is a directory, we should reopen it as a file. */
  if (dir != NULL)
    {
      file = file_open (inode_reopen (dir_get_inode (dir)));
      dir_close (dir);
      dir = NULL;
    }

  /* File found, return it. */
  free (name_copy);
  return file;

fail:

  /* Clean up and return null. */
  if (dir != NULL)
    dir_close (dir);
  if (file != NULL)
    file_close (file);
  return NULL;
}

/* Opens the directory with the given NAME.
   Returns the new directory if successful or a null pointer
   otherwise.
   Fails if no directory named NAME exists,
   or if an internal memory allocation fails. */
struct dir *
filesys_open_dir (const char *name)
{
  size_t name_len = strlen (name);
  if (name_len == 0)
    return NULL;

  return filesys_open_dir_length (name, name_len);
}

/* Deletes the file or empty directory with the given NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  /* Split directory into parent and base. */
  size_t parent_len;
  const char *base_begin = NULL;
  const char *base_end = NULL;
  path_split (name, &parent_len, &base_begin, &base_end);

  /* A file should not have an empty name. */
  size_t base_len = base_end - base_begin;
  if (base_len == 0)
    return false;

  /* Copy base name to a new string. */
  char *base_name = NULL;
  base_name = malloc (base_len + 1);
  if (base_name == NULL)
    return false;
  strlcpy (base_name, base_begin, base_len + 1);

  /* Open parent directory, then remove the file. */
  struct dir *parent_dir = filesys_open_dir_length (name, parent_len);
  bool success = parent_dir != NULL && dir_remove (parent_dir, base_name);

  /* Clean up. */
  dir_close (parent_dir);
  free (base_name);

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
