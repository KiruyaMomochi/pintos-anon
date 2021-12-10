#include "mmap.h"
#include "kernel/debug.h"
#include "page.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include <stdlib.h>

struct mmap_file *
mmap_file_create (struct file *file, void *uaddr)
{
  ASSERT (file != NULL);
  ASSERT (uaddr != NULL);
  ASSERT (((uint32_t)uaddr & PGMASK) == 0);

  file = file_reopen (file);
  if (file == NULL)
    return NULL;

  uint32_t read_bytes = file_length (file);
  uint32_t byte_cnt = (uint32_t)pg_round_up ((void *)read_bytes);
  uint32_t zero_bytes = byte_cnt - read_bytes;

  struct mmap_file *mmap_file = malloc (sizeof (struct mmap_file));
  mmap_file->page_cnt = (size_t)pg_no ((void *)byte_cnt);

  if (mmap_file == NULL)
    return NULL;

  mmap_file->file = file;
  mmap_file->uaddr = uaddr;

  if (!load_segment (file, 0, uaddr, read_bytes, zero_bytes, true, false))
    {
      file_close (file);
      free (mmap_file);
      return NULL;
    }

  return mmap_file;
}

bool
mmap_file_destroy (struct mmap_file *mmap_file)
{
  ASSERT (mmap_file != NULL);

  uint8_t *page = mmap_file->uaddr;

  for (size_t i = 0; i < mmap_file->page_cnt; i++)
    {
      bool success = supp_destroy (page);
      if (!success)
        PANIC ("supp_destroy failed");
      page += PGSIZE;
    }

  file_close (mmap_file->file);
  free (mmap_file);

  return true;
}
