#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

/* 
 * File system cache.
 * The cache is used to speed up file system operations.
 */

#include "devices/block.h"
#include "filesys/off_t.h"

/* Initialization, enabling, and disabling. */

void filesys_cache_init (void);
void filesys_cache_enable (void);
void filesys_cache_disable (void);

/* Write all dirty blocks to disk. */

void filesys_sync (void);

/* Read/write a block from/to the cache. */

void filesys_block_write (block_sector_t sector, const void *buffer);
void filesys_block_read (block_sector_t sector, void *buffer);

/* Read/write some parts in a block from/to the cache. */

void filesys_block_read_bytes (block_sector_t sector, void *buffer, off_t ofs,
                               uint32_t bytes);
void filesys_block_write_bytes (block_sector_t sector, const void *buffer,
                                off_t ofs, uint32_t bytes);

#endif // FILESYS_CACHE_H
