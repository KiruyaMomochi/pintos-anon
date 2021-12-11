#ifndef VM_SWAP_H
#define VM_SWAP_H

/* Number of sectors we need to save a page. */
#define PAGE_SECTOR_COUNT (PGSIZE / BLOCK_SECTOR_SIZE)

#include <stddef.h>

/* The swap partition.

   Our swap space is managed by a in-memory bitmap,
   where each bit represents a single page. */

void swap_init (void);
size_t swap_install (void *kpage);
void swap_uninstall (size_t index, void *kpage);
void swap_remove (size_t index);

#endif // VM_SWAP_H
