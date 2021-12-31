#include "cache.h"
#include "debug.h"
#include "filesys/filesys.h"
#include "kernel/list.h"
#include "string.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* Size of file system cache. */
#define FILESYS_CACHE_SIZE 64

/* Number of ticks to synchronize the cache. */
#define FILESYS_CACHE_TICKS 10000

/* A block cache. */
struct block_cache_elem
{
  block_sector_t sector; /* Sector number of block. */

  bool in_use; /* Is in use or free? */
  bool dirty;  /* Is dirty or clean? */
  bool access; /* Is accessed or not? */
  bool pin;    /* Is pinned or not? */

  /* Cache data, size should be BLOCK_SECTOR_SIZE. */
  uint8_t data[BLOCK_SECTOR_SIZE];
};

/* File system cache array.
   Since all global variables are initialized to zero,
   all elements are not in use initially. */
static struct block_cache_elem filesys_cache[FILESYS_CACHE_SIZE];

/* Whether file system cache is enabled or not.
   Should switch by cache_enable() and cache_disable() outside of cache.c. */
static bool cache_enabled = false;

/* Lock for file system cache.
   Only acquire/release in non-static functions. */
static struct lock filesys_cache_lock;

/* Tick counter for file system cache. */
static int64_t ticks;

/* The next write operation should be synchronized. */
static bool sync_write = false;

/* Initialize file system cache. */
void
filesys_cache_init (void)
{
  lock_init (&filesys_cache_lock);
}

/* Look up a block in file system cache.
   Return NULL if not found. */
static struct block_cache_elem *
filesys_cache_lookup (block_sector_t sector)
{
  for (int i = 0; i < FILESYS_CACHE_SIZE; i++)
    {
      if (filesys_cache[i].in_use && filesys_cache[i].sector == sector)
        return &filesys_cache[i];
    }
  return NULL;
}

/* Write back a block in file system cache. */
static void
filesys_cache_write_back (struct block_cache_elem *elem)
{
  ASSERT (elem != NULL);
  ASSERT (elem->dirty);

  block_write (fs_device, elem->sector, elem->data);
  elem->dirty = false;
}

/* Evict a block in file system cache.
   Return NULL if no block can be evicted. */
static struct block_cache_elem *
filesys_cache_evict (void)
{
  static int t = 0;

  t %= FILESYS_CACHE_SIZE;
  int end = t + FILESYS_CACHE_SIZE * 2;

  for (; t < end; t++)
    {
      int i = t % FILESYS_CACHE_SIZE;
      struct block_cache_elem *elem = &filesys_cache[i];

      /* If the block is not accessed, return it. */
      if (!elem->in_use)
        return elem;

      /* If the block is pinned, skip it. */
      if (elem->pin)
        continue;

      /* If the block is accessed, unset access and continue. */
      if (elem->access)
        {
          elem->access = false;
          continue;
        }

      /* Otherwise, write back, evict, and return. */
      if (elem->dirty)
        filesys_cache_write_back (elem);
      /* Setting in_use to false means evicting. */
      elem->in_use = false;
      return elem;
    }

  return NULL;
}

/* Access a block at SECTOR in file system cache.

   Load it first if not in cache.
   When READ is true, read from disk when loading.

   Returns NULL if memory allocation fails. */
static struct block_cache_elem *
filesys_cache_access (block_sector_t sector, bool read)
{
  struct block_cache_elem *elem = filesys_cache_lookup (sector);

  /* Add a new block to cache if not found. */
  if (elem == NULL)
    {
      elem = filesys_cache_evict ();
      if (elem == NULL)
        return false;

      elem->in_use = true;
      elem->sector = sector;
      elem->dirty = false;
      elem->access = false;
      elem->pin = false;

      if (read)
        block_read (fs_device, elem->sector, elem->data);
    }

  /* Set access and return. */
  elem->access = true;
  return elem;
}

/* Write back all dirty blocks in file system cache.
   This function does not acquire filesys_cache_lock. */
static void
filesys_sync_nolock (void)
{
  for (int i = 0; i < FILESYS_CACHE_SIZE; i++)
    {
      struct block_cache_elem *elem = &filesys_cache[i];
      if (elem->dirty)
        filesys_cache_write_back (elem);
      elem->dirty = false;
    }
}

/* Prefetch a block in file system cache. */
static void
filesys_prefetch (block_sector_t sector)
{
  filesys_cache_access (sector, true);
}

/* Write back all dirty blocks in file system cache. */
void
filesys_sync (void)
{
  lock_acquire (&filesys_cache_lock);

  if (cache_enabled)
    filesys_sync_nolock ();

  lock_release (&filesys_cache_lock);
}

/* Reads sector SECTOR from file system into BUFFER, which must
   have room for BLOCK_SECTOR_SIZE bytes.

   If the block exists in the cache, read it.
   Otherwise, load it first, then read it from cache. */
void
filesys_block_read (block_sector_t sector, void *buffer)
{
  /* If cache is disabled, read directly from disk. */
  if (!cache_enabled)
    {
      block_read (fs_device, sector, buffer);
      return;
    }

  lock_acquire (&filesys_cache_lock);

  struct block_cache_elem *elem = filesys_cache_access (sector, true);

  /* If cache accessing fails, read directly from disk. */
  if (elem == NULL)
    {
      lock_release (&filesys_cache_lock);
      block_read (fs_device, sector, buffer);
      return;
    }

  if (sector + 1 < block_size (fs_device))
    filesys_prefetch (sector + 1);

  memcpy (buffer, elem->data, BLOCK_SECTOR_SIZE);

  lock_release (&filesys_cache_lock);
}

/* Reads BYTES of sector SECTOR into BUFFER, starting at offset
   OFS_OFFSET. */
void
filesys_block_read_bytes (block_sector_t sector, void *buffer, off_t ofs,
                          uint32_t bytes)
{
  /* If cache is disabled, read directly from disk. */
  if (!cache_enabled)
    {
      uint8_t *bounce = malloc (BLOCK_SECTOR_SIZE);
      if (bounce == NULL)
        PANIC ("out of memory");
      block_read (fs_device, sector, bounce);
      memcpy (buffer, bounce + ofs, bytes);
      return;
    }

  lock_acquire (&filesys_cache_lock);

  struct block_cache_elem *elem = filesys_cache_access (sector, true);

  /* If cache accessing fails, we panics because our malloc may not
     succeed too. */
  if (elem == NULL)
    PANIC ("filesys_block_read_bytes: cache access failed");

  memcpy (buffer, elem->data + ofs, bytes);

  lock_release (&filesys_cache_lock);
}

/* Write sector SECTOR to file system from BUFFER, which must contain
   BLOCK_SECTOR_SIZE bytes.  Returns after the block device has
   acknowledged receiving the data.

   If the block exists in the cache, write it.
   Otherwise, load it first, then write it to cache. */
void
filesys_block_write (block_sector_t sector, const void *buffer)
{
  if (!cache_enabled)
    {
      block_write (fs_device, sector, buffer);
      return;
    }

  lock_acquire (&filesys_cache_lock);

  struct block_cache_elem *elem = filesys_cache_access (sector, false);
  memcpy (elem->data, buffer, BLOCK_SECTOR_SIZE);
  elem->dirty = true;

  if (sync_write)
    {
      filesys_sync_nolock ();
      sync_write = false;
    }

  lock_release (&filesys_cache_lock);
}

/* Write BYTES of sector SECTOR to file system from BUFFER, starting at
   offset OFS_OFFSET. Returns after the block device has acknowledged
   receiving the data. */
void
filesys_block_write_bytes (block_sector_t sector, const void *buffer,
                           off_t ofs, uint32_t bytes)
{
  /* If cache is disabled, write directly to disk. */
  if (!cache_enabled)
    {
      uint8_t *bounce = malloc (BLOCK_SECTOR_SIZE);
      if (bounce == NULL)
        PANIC ("out of memory");
      block_read (fs_device, sector, bounce);
      memcpy (bounce + ofs, buffer, bytes);
      block_write (fs_device, sector, bounce);
      return;
    }

  lock_acquire (&filesys_cache_lock);

  struct block_cache_elem *elem = filesys_cache_access (sector, true);
  memcpy (elem->data + ofs, buffer, bytes);
  elem->dirty = true;

  if (sync_write)
    {
      filesys_sync_nolock ();
      sync_write = false;
    }

  lock_release (&filesys_cache_lock);
}

/* Enable file system cache. */
void
filesys_cache_enable (void)
{
  lock_acquire (&filesys_cache_lock);

  if (!cache_enabled)
    cache_enabled = true;

  lock_release (&filesys_cache_lock);
}

/* Disable file system cache. */
void
filesys_cache_disable (void)
{
  lock_acquire (&filesys_cache_lock);

  if (cache_enabled)
    {
      filesys_sync_nolock ();
      cache_enabled = false;
    }

  lock_release (&filesys_cache_lock);
}

void
filesys_cache_tick (void)
{
  ticks++;
  if (ticks % FILESYS_CACHE_TICKS == 0)
    sync_write = true;
}
