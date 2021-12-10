#include "frame.h"
#include "kernel/debug.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "utils/colors.h"
#include <random.h>
#include <stdio.h>

/* Frame table. */
struct list frame_table;

/* Initializes the frame table. */
void
frame_init (void)
{
  list_init (&frame_table);
}

/* Looks up the frame with given kernel page KPAGE. */
struct supp_entry *
frame_lookup (const void *kpage)
{
  ASSERT (kpage != NULL);
  ASSERT (is_kernel_vaddr (kpage));

  struct list_elem *e;

  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      struct supp_entry *f = list_entry (e, struct supp_entry, frame_elem);
      if (f->kpage == kpage)
        return f;
    }
  return NULL;
}

static void
frame_insert (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  list_push_back (&frame_table, &entry->frame_elem);
}

void
frame_remove (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  ASSERT (supp_is_loaded (entry));

  list_remove (&entry->frame_elem);
}

bool
frame_allocate (enum palloc_flags flags, struct supp_entry *entry)
{
  ASSERT (!supp_is_loaded (entry));
  ASSERT ((flags & PAL_USER));

  void *kpage = palloc_get_page (flags);
  if (kpage == NULL)
    return false;

  supp_set_kpage (entry, kpage);
  return true;
}


void
frame_free (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  ASSERT (supp_is_loaded (entry));
  ASSERT (entry->kpage != NULL);
  ASSERT (entry->upage != NULL);

  palloc_free_page (entry->kpage);
  supp_set_kpage (entry, NULL);
}

bool
frame_install (struct supp_entry *entry)
{
  ASSERT (entry->kpage != NULL);
  ASSERT (entry->upage != NULL);
  ASSERT (!supp_is_loaded (entry));

  void *current_upage = pagedir_get_page (entry->owner->pagedir, entry->upage);
  if (current_upage != NULL)
    return false;

  bool success = pagedir_set_page (entry->owner->pagedir, entry->upage,
                                   entry->kpage, entry->writable);
  if (success)
    frame_insert (entry);

  return success;
}

void
frame_uninstall (struct supp_entry *entry)
{
  ASSERT (entry->kpage != NULL);
  ASSERT (entry->upage != NULL);
  ASSERT (supp_is_loaded (entry));

  pagedir_clear_page (entry->owner->pagedir, entry->upage);
  frame_remove (entry);
}
