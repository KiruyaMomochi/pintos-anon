#include "filesys/inode.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <string.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Number of sectors to allocate for an inode */
#define INODE_BLOCK_COUNT 124

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  off_t length;    /* Length of the inode. */
  uint32_t depth;  /* Depth of the inode. */
  uint32_t is_dir; /* Whether the inode is a file or not. */
  block_sector_t blocks[INODE_BLOCK_COUNT]; /* Data blocks. */

  unsigned magic; /* Magic number. */
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the level of depth needed to store for an inode SIZE
   bytes long. */
static uint32_t
bytes_to_depth (off_t size)
{
  size_t sectors = bytes_to_sectors (size);
  uint32_t depth = 0;
  while (sectors > INODE_BLOCK_COUNT)
    {
      sectors = DIV_ROUND_UP (sectors, INODE_BLOCK_COUNT);
      depth++;
    }
  return depth;
}

/* In-memory inode. */
struct inode
{
  struct list_elem elem;  /* Element in inode list. */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct inode_disk data; /* Inode content. */
};

/* inode_disk calculation functions. */

/* Returns number of sectors in each block. */
static size_t
inode_disk_block_sectors (const struct inode_disk *inode_disk)
{
  size_t sectors = 1;
  size_t depth = inode_disk->depth;

  while (depth > 0)
    {
      sectors *= INODE_BLOCK_COUNT;
      depth--;
    }
  return sectors;
}

/* Returns maximum bytes can be stored in each block. */
static off_t
inode_disk_max_block_size (const struct inode_disk *inode_disk)
{
  return inode_disk_block_sectors (inode_disk) * BLOCK_SECTOR_SIZE;
}

/* Returns the number of blocks to allocate or allocated for an inode. */
static inline size_t
inode_disk_blocks (const struct inode_disk *inode_disk)
{
  return DIV_ROUND_UP (inode_disk->length,
                       inode_disk_max_block_size (inode_disk));
}

/* Returns maximum bytes can be stored in this inode. */
static off_t
inode_disk_max_size (const struct inode_disk *inode_disk)
{
  return inode_disk_block_sectors (inode_disk) * BLOCK_SECTOR_SIZE
         * INODE_BLOCK_COUNT;
}

/* Returns bytes of data stored in sector with index POS. */
static off_t
inode_disk_block_size (const struct inode_disk *inode_disk, size_t pos)
{
  ASSERT (inode_disk != NULL);

  off_t max_block_size = inode_disk_max_block_size (inode_disk);
  size_t full_blocks = inode_disk->length / max_block_size;

  if (pos <= full_blocks)
    return max_block_size;

  if (pos == full_blocks + 1)
    return inode_disk->length % max_block_size;

  return 0;
}

/* Returns the block device sector that contains byte offset POS
   within direct disk inode INODE_DISK.
   Returns -1 if INODE_DISK does not contain data for a byte at
   offset POS. */
static block_sector_t
inode_disk_byte_to_sector_direct (const struct inode_disk *inode_disk,
                                  off_t pos)
{
  ASSERT (inode_disk != NULL);
  ASSERT (inode_disk->depth == 0);

  if (pos < inode_disk->length)
    return inode_disk->blocks[pos / BLOCK_SECTOR_SIZE];
  else
    return -1;
}

/* Returns the block device sector that contains byte offset POS
   within disk inode INODE_DISK.
   Returns -1 if INODE_DISK does not contain data for a byte at
   offset POS, or memory allocation fails. */
static block_sector_t
inode_disk_byte_to_sector (const struct inode_disk *inode_disk, off_t pos)
{
  ASSERT (inode_disk != NULL);
  if (inode_disk->depth == 0)
    return inode_disk_byte_to_sector_direct (inode_disk, pos);

  struct inode_disk *indirect_indoe_disk = NULL;
  indirect_indoe_disk = malloc (sizeof *indirect_indoe_disk);
  if (indirect_indoe_disk == NULL)
    return -1;

  /* Find the indirect block to read. */
  off_t max_block_size = inode_disk_max_block_size (inode_disk);
  size_t block_index = pos / max_block_size;
  size_t block_pos = pos % max_block_size;

  if (block_index >= INODE_BLOCK_COUNT)
    {
      free (indirect_indoe_disk);
      return -1;
    }

  /* Recursively find the sector in the indirect block. */
  filesys_block_read (inode_disk->blocks[block_index], indirect_indoe_disk);
  block_sector_t sector
      = inode_disk_byte_to_sector (indirect_indoe_disk, block_pos);

  free (indirect_indoe_disk);
  return sector;
}

/* Create an empty inode structure with depth of DEPTH at SECTOR.
   When IS_DIR is true, the inode is a directory.
   Returns true if successful, false on failure. */
static bool
inode_create_empty (block_sector_t sector, uint32_t depth, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode == NULL)
    return false;

  disk_inode->length = 0;
  disk_inode->is_dir = is_dir;
  disk_inode->depth = depth;
  disk_inode->magic = INODE_MAGIC;

  filesys_block_write (sector, disk_inode);
  free (disk_inode);

  return true;
}

/* Grow functions for inode. */

static bool inode_disk_grow_length_direct (struct inode_disk *disk_inode,
                                           off_t size, bool zero);
static bool inode_disk_grow_length (struct inode_disk *disk_inode,
                                    off_t length, bool zero);
static bool inode_grow_depth (struct inode *inode, size_t depth);

static bool sector_grow_length (block_sector_t sector, off_t length,
                                bool zero);

/* Grow the length of the direct inode DISK_INODE to SIZE bytes.
   If ZERO is true, zero the new space.
   Returns true if successful, false on failure. */
static bool
inode_disk_grow_length_direct (struct inode_disk *disk_inode, off_t size,
                               bool zero)
{
  ASSERT (disk_inode != NULL);
  ASSERT (disk_inode->depth == 0);

  if (size < disk_inode->length)
    return false;
  if (size == disk_inode->length)
    return true;

  size_t old_sectors = bytes_to_sectors (disk_inode->length);
  size_t new_sectors = bytes_to_sectors (size);

  if (new_sectors > INODE_BLOCK_COUNT)
    return false;

  /* Increase the number of blocks from old_sectors to new_sectors. */
  size_t i = old_sectors;
  for (i = old_sectors; i < new_sectors; i++)
    {
      if (!free_map_allocate (1, disk_inode->blocks + i))
        break;

      if (zero)
        {
          static char zeros[BLOCK_SECTOR_SIZE];
          filesys_block_write (disk_inode->blocks[i], zeros);
        }
    }

  /* If we failed to allocate some blocks,
     free them and return false. */
  if (i != new_sectors)
    {
      for (size_t j = old_sectors; j < i; j++)
        free_map_release (disk_inode->blocks[j], 1);
      return false;
    }

  /* Update the length of the inode. */
  disk_inode->length = size;
  return true;
}

/* Grow the depth of the inode DISK_INODE to DEPTH.
   Returns true if successful, false on failure.

   This functions works by
     1. Copy the old inode to a new block.
     2. Recursively create new inode pointing to the last inode,
        with depth increased by one.
     3. Set the original inode to the new inode. */
static bool
inode_grow_depth (struct inode *inode, size_t depth)
{
  ASSERT (inode != NULL);
  size_t old_depth = inode->data.depth;

  if (old_depth > depth)
    return false;
  if (old_depth == depth)
    return true;

  /* Get the old indirect block. */
  block_sector_t sector;
  struct inode_disk disk_inode = inode->data;

  /* Write the old block to new sector. */
  if (!free_map_allocate (1, &sector))
    return false;
  filesys_block_write (sector, &disk_inode);

  /* Set all the new indirect blocks to 0. */
  for (size_t i = 0; i < INODE_BLOCK_COUNT; i++)
    disk_inode.blocks[i] = 0;
  /* Increase the depth of the inode. */
  disk_inode.depth++;
  /* Write the new sector to the inode. */
  disk_inode.blocks[0] = sector;

  /* Recursively write a new block, whose sector is the last block,
     with depth increased by one. */
  while (disk_inode.depth < depth)
    {
      /* Return false when allocation fails.
         Previous growths are kept. */
      if (!free_map_allocate (1, &sector))
        return false;

      /* Write the new block to the inode. */
      filesys_block_write (sector, &disk_inode);

      /* Increase the depth of the inode. */
      disk_inode.depth++;

      /* Target the old sector. */
      disk_inode.blocks[0] = sector;
    }

  /* The last block should be saved to the original inode. */
  inode->data = disk_inode;
  filesys_block_write (inode->sector, &inode->data);

  return true;
}

/* Grow the length of the inode DISK_INODE to SIZE bytes.
   If ZERO is true, zero the new space.
   Returns true if successful, false on failure. */
static bool
inode_disk_grow_length (struct inode_disk *disk_inode, off_t length, bool zero)
{
  ASSERT (disk_inode != NULL);

  if (disk_inode->depth == 0)
    return inode_disk_grow_length_direct (disk_inode, length, zero);
  if (length < disk_inode->length)
    return false;
  if (length == disk_inode->length)
    return true;

  off_t max_block_size = inode_disk_max_block_size (disk_inode);
  off_t length_to_grow = length - disk_inode->length;

  /* Allocated a block but it is not used. */
  bool last_allocated = false;

  /* Current index of the block. */
  size_t block_index = 0;
  /* The length of current block. */
  size_t block_length = 0;

  while (disk_inode->length != length)
    {
      block_index = disk_inode->length / max_block_size;
      block_length = disk_inode->length % max_block_size;

      /* Find block length after growing. */
      off_t new_block_length = block_length + length_to_grow;
      if (new_block_length > max_block_size)
        new_block_length = max_block_size;

      /* Zero-length block are not allocated, so we need to allocate
         a new block, and create a new block pointing to it. */
      if (block_length == 0)
        {
          if (!free_map_allocate (1, disk_inode->blocks + block_index))
            break;

          last_allocated = true;
          if (!inode_create_empty (disk_inode->blocks[block_index],
                                   disk_inode->depth - 1, false))
            break;
        }

      /* Recursively grow the indirect block. */
      if (!sector_grow_length (disk_inode->blocks[block_index],
                               new_block_length, zero))
        break;

      /* Update length of the inode. */
      disk_inode->length
          = disk_inode->length + (new_block_length - block_length);
      length_to_grow = length - disk_inode->length;

      last_allocated = false;
    }

  /* If the last allocated block is not used, free it. */
  if (last_allocated)
    free_map_release (disk_inode->blocks[block_index], 1);

  if (disk_inode->length != length)
    return false;

  return true;
}

/* Grow the length of inode at sector SECTOR to LENGTH bytes.
   If ZERO is true, zero the new space.
   Returns true if successful, false on failure. */
static bool
sector_grow_length (block_sector_t sector, off_t length, bool zero)
{
  struct inode_disk *disk_inode = NULL;

  disk_inode = malloc (sizeof *disk_inode);
  if (disk_inode == NULL)
    return false;
  filesys_block_read (sector, disk_inode);

  bool success = inode_disk_grow_length (disk_inode, length, zero);

  filesys_block_write (sector, disk_inode);
  free (disk_inode);

  return success;
}

/* Grow the length of inode INODE to LENGTH bytes.
   If ZERO is true, zero the new space.
   Returns true if successful, false on failure. */
static bool
inode_grow_length (struct inode *inode, off_t length, bool zero)
{
  bool success = inode_disk_grow_length (&inode->data, length, zero);
  filesys_block_write (inode->sector, &inode->data);
  return success;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
inode_byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  return inode_disk_byte_to_sector (&inode->data, pos);
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
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  uint32_t depth = bytes_to_depth (length);
  if (!inode_create_empty (sector, depth, is_dir))
    return false;
  if (!sector_grow_length (sector, length, true))
    return false;
  return true;
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
  filesys_block_read (inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Remove the direct inode from the file system device. */
static void
inode_disk_remove_direct (struct inode_disk *disk_inode)
{
  ASSERT (disk_inode->depth == 0);

  size_t sectors = bytes_to_sectors (disk_inode->length);
  for (size_t i = 0; i < sectors; i++)
    free_map_release (disk_inode->blocks[i], 1);
}

/* Remove the inode from the file system device. */
static void
inode_disk_remove (struct inode_disk *disk_inode)
{
  if (disk_inode->depth == 0)
    {
      inode_disk_remove_direct (disk_inode);
      return;
    }

  struct inode_disk *indirect_disk_inode = NULL;
  indirect_disk_inode = malloc (sizeof *indirect_disk_inode);
  if (indirect_disk_inode == NULL)
    PANIC ("inode_disk_remove: malloc failed");

  /* Recursively remove the indirect blocks. */
  size_t allocated_blocks = inode_disk_blocks (disk_inode);
  for (size_t i = 0; i < allocated_blocks; i++)
    {
      filesys_block_read (disk_inode->blocks[i], indirect_disk_inode);
      free_map_release (disk_inode->blocks[i], 1);
      inode_disk_remove (indirect_disk_inode);
    }

  free (indirect_disk_inode);
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt != 0)
    return;

  /* Remove from inode list and release lock. */
  list_remove (&inode->elem);

  /* Deallocate blocks if removed. */
  if (inode->removed)
    {
      free_map_release (inode->sector, 1);
      inode_disk_remove (&inode->data);
    }

  free (inode);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from direct disk inode INODE_DISK into BUFFER,
   starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
static off_t
inode_disk_read_at_direct (struct inode_disk *inode_disk, void *buffer_,
                           off_t size, off_t offset)
{
  ASSERT (inode_disk->depth == 0);

  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx
          = inode_disk_byte_to_sector_direct (inode_disk, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_disk->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector into caller's buffer. */
          filesys_block_read (sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read bytes into caller's buffer. */
          filesys_block_read_bytes (sector_idx, buffer + bytes_read,
                                    sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Reads SIZE bytes from INODE_DISK into BUFFER, starting at
   position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
static off_t
inode_disk_read_at (struct inode_disk *inode_disk, void *buffer_, off_t size,
                    off_t offset)
{
  if (inode_disk->depth == 0)
    return inode_disk_read_at_direct (inode_disk, buffer_, size, offset);

  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  struct inode_disk *indirect_disk_inode = NULL;
  indirect_disk_inode = malloc (sizeof *indirect_disk_inode);
  if (indirect_disk_inode == NULL)
    return bytes_read;

  off_t max_block_size = inode_disk_max_block_size (inode_disk);

  while (size > 0)
    {
      size_t block_index = offset / max_block_size;
      size_t block_offset = offset % max_block_size;
      if (block_index >= inode_disk_blocks (inode_disk))
        PANIC ("inode_disk_read_at: block_index out of range");

      /* Find size in the block to read. */
      off_t inode_left = inode_disk->length - offset;
      off_t block_left
          = inode_disk_block_size (inode_disk, block_index) - block_offset;
      off_t min_left = inode_left < block_left ? inode_left : block_left;
      off_t read_size = size < min_left ? size : min_left;

      if (read_size <= 0)
        break;

      /* Read the block. */
      filesys_block_read (inode_disk->blocks[block_index],
                          indirect_disk_inode);
      read_size = inode_disk_read_at (indirect_disk_inode, buffer + bytes_read,
                                      read_size, block_offset);

      /* Advance. */
      size -= read_size;
      offset += read_size;
      bytes_read += read_size;
    }

  free (indirect_disk_inode);

  return bytes_read;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  return inode_disk_read_at (&inode->data, buffer_, size, offset);
}

/* Writes SIZE bytes from BUFFER into direct disk inode INODE_DISK,
   starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs. */
static off_t
inode_disk_write_at_direct (struct inode_disk *inode_disk, const void *buffer_,
                            off_t size, off_t offset)
{
  ASSERT (inode_disk->depth == 0);

  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx
          = inode_disk_byte_to_sector_direct (inode_disk, offset);
      if (sector_idx == (block_sector_t)-1)
        break;

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_disk->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector to disk. */
          filesys_block_write (sector_idx, buffer + bytes_written);
        }
      else
        {
          /* Write bytes to disk. */
          filesys_block_write_bytes (sector_idx, buffer + bytes_written,
                                     sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Writes SIZE bytes from BUFFER into disk inode INODE_DISK, starting
   at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs. */
static off_t
inode_disk_write_at (struct inode_disk *inode_disk, const void *buffer_,
                     off_t size, off_t offset)
{
  if (inode_disk->depth == 0)
    return inode_disk_write_at_direct (inode_disk, buffer_, size, offset);

  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  struct inode_disk *indirect_disk_inode = NULL;
  indirect_disk_inode = malloc (sizeof *indirect_disk_inode);
  if (indirect_disk_inode == NULL)
    return bytes_written;

  off_t max_block_size = inode_disk_max_block_size (inode_disk);

  while (size > 0)
    {
      size_t block_index = offset / max_block_size;
      size_t block_offset = offset % max_block_size;
      if (block_index >= inode_disk_blocks (inode_disk))
        break;

      /* Find size in the block to write. */
      off_t inode_left = inode_disk->length - offset;
      off_t block_left
          = inode_disk_block_size (inode_disk, block_index) - block_offset;
      off_t min_left = inode_left < block_left ? inode_left : block_left;
      off_t read_size = size < min_left ? size : min_left;

      if (read_size <= 0)
        break;

      off_t write_size = size > max_block_size ? max_block_size : size;

      /* Write the block. */
      filesys_block_read (inode_disk->blocks[block_index],
                          indirect_disk_inode);
      write_size
          = inode_disk_write_at (indirect_disk_inode, buffer + bytes_written,
                                 write_size, block_offset);

      /* Advance. */
      size -= write_size;
      offset += write_size;
      bytes_written += write_size;
    }

  free (indirect_disk_inode);

  return bytes_written;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   A write at end of file would extend the inode
   Returns the number of bytes actually written, which may be
   less than SIZE if an error occurs. */
off_t
inode_write_at (struct inode *inode, const void *buffer, off_t size,
                off_t offset)
{
  if (inode->deny_write_cnt)
    return 0;

  off_t new_length = offset + size;

  /* Grow depth if necessary. */
  uint32_t depth = bytes_to_depth (new_length);
  if (inode->data.depth < depth)
    {
      if (!inode_grow_depth (inode, depth))
        return 0;
    }

  /* Extend length to offset, with zero fill, if necessary. */
  if (inode->data.length < offset)
    {
      if (!inode_grow_length (inode, offset, true))
        return 0;
    }

  /* Extend length to new_length, without zero fill, if necessary. */
  if (inode->data.length < new_length)
    {
      if (!inode_grow_length (inode, new_length, false))
        return 0;
    }

  /* Actually write. */
  return inode_disk_write_at (&inode->data, buffer, size, offset);
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

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* Returns true if INODE is a directory, false if it represents an
   ordinary file. */
bool
inode_is_dir (const struct inode *inode)
{
  return inode->data.is_dir;
}
