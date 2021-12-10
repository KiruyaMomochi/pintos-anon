#include "swap.h"
#include "devices/block.h"
#include "kernel/debug.h"
#include "threads/vaddr.h"
#include <bitmap.h>

/* The swap block device. */
struct block *swap_block;

/* The swap bitmap.
   A bit is set if the corresponding page is in swap. */
struct bitmap *swap_bitmap;

block_sector_t swap_sector_count;
size_t swap_page_count;
size_t swap_total_size;

/* Initialize swap table. */
void
swap_init (void)
{
  /* Get the swap block device. */
  swap_block = block_get_role (BLOCK_SWAP);
  ASSERT (swap_block != NULL);

  /* Find out size of swap block in number of pages. */
  swap_sector_count = block_size (swap_block);
  swap_page_count = swap_sector_count / PAGE_SECTOR_COUNT;

  /* Verify the calculation is correct. */
  swap_total_size = swap_sector_count * BLOCK_SECTOR_SIZE;
  ASSERT (swap_total_size / PGSIZE == swap_page_count);

  /* Create swap bitmap. */
  swap_bitmap = bitmap_create (swap_page_count);
}

/* Convert page index to sector index. */
static size_t
page_index_to_sector (size_t index)
{
  ASSERT (index < swap_page_count);
  return index * PAGE_SECTOR_COUNT;
}

/* Write a PAGE into BLOCK at page index PAGE_INDEX. */
static void
block_write_page (struct block *block, size_t page_index, const uint8_t *page)
{
  ASSERT (block != NULL);
  ASSERT (page != NULL);

  block_sector_t sector = page_index_to_sector (page_index);

  for (size_t i = 0; i < PAGE_SECTOR_COUNT; i++)
    block_write (block, sector + i, page + i * BLOCK_SECTOR_SIZE);
}

/* Read a PAGE from BLOCK at page index PAGE_INDEX. */
static void
block_read_page (struct block *block, size_t page_index, uint8_t *page)
{
  ASSERT (block != NULL);
  ASSERT (page != NULL);

  block_sector_t sector = page_index_to_sector (page_index);

  for (size_t i = 0; i < PAGE_SECTOR_COUNT; i++)
    block_read (block, sector + i, page + i * BLOCK_SECTOR_SIZE);
}

/* Install kernel page KPAGE into swap partition.
   Return the page index of the page in swap. */
size_t
swap_install (void *kpage)
{
  ASSERT (kpage != NULL);
  ASSERT (is_kernel_vaddr (kpage));

  size_t index = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);

  DEBUG_PRINT ("kpage: %p -> #%zu", kpage, index);

  if (index == BITMAP_ERROR)
    PANIC ("No swap space available");

  block_write_page (swap_block, index, kpage);
  return index;
}

/* Uninstall the page at INDEX from swap partition.
   Read the page from swap partition into KPAGE. */
void
swap_uninstall (size_t index, void *kpage)
{
  ASSERT (index < swap_page_count);
  ASSERT (kpage != NULL);
  ASSERT (is_kernel_vaddr (kpage));
  ASSERT (bitmap_test (swap_bitmap, index));

  DEBUG_PRINT ("#%zu -> kpage: %p", index, kpage);

  block_read_page (swap_block, index, kpage);
  bitmap_set (swap_bitmap, index, false);
}

/* Remove the page at INDEX from swap partition. */
void
swap_remove (size_t index)
{
  ASSERT (index < swap_page_count);
  ASSERT (bitmap_test (swap_bitmap, index));

  DEBUG_PRINT ("#%zu", index);

  bitmap_set (swap_bitmap, index, false);
}
