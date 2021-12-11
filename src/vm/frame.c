#include "frame.h"
#include "kernel/debug.h"
#include "swap.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "utils/colors.h"
#include <random.h>
#include <stdio.h>

/* Frame table. */
struct list frame_table;

/* Lock for frame table.
   Acquiring this lock is required before modifying the frame table.*/
struct lock frame_lock;

/* Initializes the frame table. */
void
frame_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_lock);
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

/* Insert ENTRY into the frame table. */
static void
frame_insert (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  ASSERT (entry->kpage != NULL);

  lock_acquire (&frame_lock);
  list_push_back (&frame_table, &entry->frame_elem);
  lock_release (&frame_lock);
}

/* Remove ENTRY from the frame table. */
void
frame_remove (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  ASSERT (supp_is_loaded (entry));

  lock_acquire (&frame_lock);
  list_remove (&entry->frame_elem);
  lock_release (&frame_lock);
}

/* Choose a victim frame from the frame table, by random strategy. */
static struct supp_entry *
frame_choose_victim_random ()
{  
  lock_acquire (&frame_lock);

  ASSERT (!list_empty (&frame_table));

  size_t size = list_size (&frame_table);
  random_init (size);
  size_t index = random_ulong () % size;

  struct list_elem *e = list_begin (&frame_table);
  for (size_t i = 0; i < index; i++)
    e = list_next (e);
  struct supp_entry *f = list_entry (e, struct supp_entry, frame_elem);

  while (supp_is_pinned (f))
    {
      if (e == list_end (&frame_table))
        e = list_begin (&frame_table);
      else
        e = list_next (e);

      f = list_entry (e, struct supp_entry, frame_elem);
    }

  ASSERT (f != NULL);
  lock_release (&frame_lock);

  return f;
}

/* Choose a victim frame from the frame table, by second chance strategy. */
static struct supp_entry *
frame_choose_victim_second_chance ()
{
  lock_acquire (&frame_lock);

  ASSERT (!list_empty (&frame_table));

  while (true)
    {
      struct list_elem *e = list_pop_front (&frame_table);
      struct supp_entry *f = list_entry (e, struct supp_entry, frame_elem);

      if (supp_is_pinned (f))
        {
          list_push_back (&frame_table, e);
          continue;
        }

      if (supp_is_accessed (f))
        {
          supp_set_accessed (f, false);
          list_push_back (&frame_table, e);
          continue;
        }

      list_push_back (&frame_table, e);
      lock_release (&frame_lock);
      return f;
    }
}

/* Evicts a frame from the frame table, returns the victim entry. */
static struct supp_entry *
frame_evict ()
{
  struct supp_entry *victim = frame_choose_victim_second_chance ();

  DEBUG_THREAD ("evicting frame %p from %s(%d)", victim, victim->owner->name, victim->owner->tid);
  ASSERT (victim != NULL);
  ASSERT (!supp_is_pinned (victim));
  ASSERT (supp_is_loaded (victim));

  if (supp_is_mmap (victim))
    {
      supp_unload (victim);
    }
  else
    {
      supp_swap (victim);
    }

  return victim;
}

/* Allocates a frame with FLAGS and set kpage in ENTRY.
   Returns true if successful, false if memory is full. */
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

/* Allocates a frame with FLAGS and set kpage in ENTRY.
   When memory is full, evict a frame from the frame table.
   This function always returns true. */
bool
frame_allocate_swap (enum palloc_flags flags, struct supp_entry *entry)
{
  ASSERT (!supp_is_loaded (entry));
  ASSERT ((flags & PAL_USER));

  bool result = frame_allocate (flags, entry);
  while (!result)
    {
      DEBUG_PRINT ("Memory is full, evicting one.");
      frame_evict ();
      result = frame_allocate (flags, entry);
    }

  return result;
}

/* Free the kpage in ENTRY. */
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

/* Install kpage in ENTRY into the frame table. */
bool
frame_install (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  ASSERT (entry->kpage != NULL);
  ASSERT (entry->upage != NULL);
  ASSERT (!supp_is_loaded (entry));
  ASSERT (entry->owner->pagedir != NULL);
  
  void *current_upage = pagedir_get_page (entry->owner->pagedir, entry->upage);
  if (current_upage != NULL)
    return false;

  bool success = pagedir_set_page (entry->owner->pagedir, entry->upage,
                                   entry->kpage, entry->writable);
  if (success)
    frame_insert (entry);

  return success;
}

/* Uninstall kpage in ENTRY from the frame table. */
void
frame_uninstall (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  ASSERT (supp_is_loaded (entry));
  ASSERT (entry->kpage != NULL);
  ASSERT (entry->upage != NULL);
  ASSERT (entry->owner->pagedir != NULL);

  pagedir_clear_page (entry->owner->pagedir, entry->upage);
  frame_remove (entry);
}

/* Print the frame table. */
void
frame_print_table (void)
{
  printf (COLOR_YEL);
  printf ("Frame table:\n");
  printf (COLOR_RESET);

  struct list_elem *e;
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      struct supp_entry *f = list_entry (e, struct supp_entry, frame_elem);
      supp_print_entry (f);
    }
}
