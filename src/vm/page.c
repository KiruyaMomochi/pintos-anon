#include "page.h"
#include "frame.h"
#include "kernel/debug.h"
#include "swap.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "utils/colors.h"
#include <stdio.h>
#include <string.h>

/* Hash helper functions */
static unsigned supp_hash (const struct hash_elem *e,
                           void *aux __attribute__ ((unused)));
static bool supp_less (const struct hash_elem *a, const struct hash_elem *b,
                       void *aux __attribute__ ((unused)));

static struct supp_entry *supp_insert_entry (struct supp_table *table,
                                             struct supp_entry *entry);
static struct supp_entry *supp_remove_entry (struct supp_table *table,
                                             struct supp_entry *entry);
static struct supp_entry *supp_find_entry (struct supp_table *table,
                                           void *upage);

static struct supp_entry *supp_insert_base (void *upage, bool writable);
static bool supp_destroy_entry (struct supp_entry *entry);
static void supp_set_state (struct supp_entry *entry, enum supp_state state);
static void supp_set_swap (struct supp_entry *entry, size_t swap_index);
static void supp_write_mmap (struct supp_entry *entry);
static bool supp_load_file (struct supp_entry *entry);
static bool supp_load_normal (struct supp_entry *entry);
static void supp_unswap (struct supp_entry *entry);

/* Hash table operations */

/* Hash helper functions. We use user page as the key. */
static unsigned
supp_hash (const struct hash_elem *e, void *aux __attribute__ ((unused)))
{
  struct supp_entry *entry = hash_entry (e, struct supp_entry, supp_elem);
  return hash_int ((int)(entry->upage));
}

/* Hash comparison function. We say an entry is less than another if its
   user page is less than the other's. */
static bool
supp_less (const struct hash_elem *a, const struct hash_elem *b,
           void *aux __attribute__ ((unused)))
{
  struct supp_entry *entry_a = hash_entry (a, struct supp_entry, supp_elem);
  struct supp_entry *entry_b = hash_entry (b, struct supp_entry, supp_elem);
  return entry_a->upage < entry_b->upage;
}

/* Initialize a supplemental TABLE.
   It just initializes the hash table. */
void
supp_init (struct supp_table *table)
{
  hash_init (&table->hash, supp_hash, supp_less, NULL);
}

/* Insert ENTRY into supplemental table TABLE.
   Assumes ENTRY is not already in TABLE.
   Returns the inserted entry. */
static struct supp_entry *
supp_insert_entry (struct supp_table *table, struct supp_entry *entry)
{
  ASSERT (table != NULL);
  ASSERT (entry != NULL);
  ASSERT (hash_find (&table->hash, &entry->supp_elem) == NULL);

  struct supp_entry *result = hash_insert (&table->hash, &entry->supp_elem);
  ASSERT (result == NULL);

  return entry;
}

/* Remove ENTRY from supplemental table TABLE.
   Returns the removed entry, or a null pointer if entry is not in table. */
static struct supp_entry *
supp_remove_entry (struct supp_table *table, struct supp_entry *entry)
{
  ASSERT (table != NULL);
  ASSERT (entry != NULL);

  struct supp_entry *result = hash_delete (&table->hash, &entry->supp_elem);
  return result == NULL ? NULL : entry;
}

/* Find an entry in supplemental table TABLE with user page UPAGE.
   Returns a pointer to the entry, or a null pointer if no entry is found. */
static struct supp_entry *
supp_find_entry (struct supp_table *table, void *upage)
{
  struct supp_entry entry;
  struct hash_elem *e;

  ASSERT (table != NULL);
  ASSERT (upage != NULL);
  ASSERT (is_user_vaddr (upage));

  entry.upage = upage;
  e = hash_find (&table->hash, &entry.supp_elem);

  return e != NULL ? hash_entry (e, struct supp_entry, supp_elem) : NULL;
}

/* General */

/* Find an entry in current process's supplemental table with user page UPAGE.
   Returns a pointer to the entry, or a null pointer if no entry is found. */
struct supp_entry *
supp_find (void *upage)
{
  return supp_find_entry (&process_current ()->supp_table, upage);
}

/* Unload ENTRY from current process's supplemental table.

   State: Loaded -> Not Loaded
   Type:  All types */
void
supp_unload (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  ASSERT (supp_is_loaded (entry));

  /* Write back dirty and mmap-ed pages */
  if (supp_is_mmap (entry) && supp_is_dirty (entry))
    supp_write_mmap (entry);

  frame_uninstall (entry);
  frame_free (entry);

  supp_set_state (entry, NOT_LOADED);
}

/* Common operations when inserting a new entry.
   Allocate a new entry and insert it into current process's
   supplemental table. The type of the new entry is SUPP_NORMAL,
   and may be changed later.
   Return the new entry, or a null pointer if no memory is
   available. */
static struct supp_entry *
supp_insert_base (void *upage, bool writable)
{
  ASSERT (upage != NULL);

  if (supp_find (upage) != NULL)
    return NULL;

  /* Malloc use kernel pool */
  struct supp_entry *entry = malloc (sizeof (struct supp_entry));
  if (entry == NULL)
    return NULL;

  entry->state = NOT_LOADED;
  entry->type = SUPP_NORMAL;
  entry->kpage = NULL;
  entry->upage = upage;

  entry->owner = thread_current ();
  entry->writable = writable;
  entry->pinned = false;
  entry->dirty = false;

  entry->file = NULL;

  return supp_insert_entry (&process_current ()->supp_table, entry);
}

/* Destroy an entry. If it's loaded, unload it first.
   Return true if the entry is destroyed successfully. */
static bool
supp_destroy_entry (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  ASSERT (supp_is_not_loaded (entry) || supp_is_loaded (entry));

  struct process *process = process_current ();
  ASSERT (process != NULL && process->thread == entry->owner);

  if (supp_is_loaded (entry))
    supp_unload (entry);

  if (supp_remove_entry (&process->supp_table, entry) == NULL)
    return false;

  free (entry);

  return true;
}

/* Destroy entry with UPAGE from current process's supplemental table.
   Return true if the entry is destroyed successfully. */
bool
supp_destroy (void *upage)
{
  ASSERT (upage != NULL);
  struct supp_entry *entry = supp_find (upage);
  return supp_destroy_entry (entry);
}

/* Hash helper for removing entry from supplemental hash table. */
static void
supp_remove_action (struct hash_elem *e, void *aux)
{
  struct supp_entry *entry = hash_entry (e, struct supp_entry, supp_elem);
  uint32_t *pd = aux;

  if (supp_is_mmap (entry) && supp_is_loaded (entry))
    {
      bool dirty = entry->dirty || pagedir_is_dirty (pd, entry->upage);
      if (dirty)
        supp_write_mmap (entry);
    }

  if (supp_is_loaded (entry))
    frame_remove (entry);
}

/* Remove all entries from supplemental table of current process.
   Using PD as the page directory, and destroy it. */
void
supp_remove_all (uint32_t *pd)
{
  struct process *process = process_current ();
  struct hash *hash = &process->supp_table.hash;
  hash->aux = pd;

  hash_clear (hash, supp_remove_action);
  pagedir_destroy (pd);
}

bool
supp_handle_page_fault (void *fault_page)
{
  /* The address should be page aligned. */
  ASSERT (((uint32_t)fault_page & PGMASK) == 0);

  /* Ignore null pointer. */
  if (fault_page == NULL)
    return false;

  /* Ignore page faults in kernel */
  if (is_kernel_vaddr (fault_page))
    return false;

  /* Ignore if page does not exist in
     current process's supplemental table. */
  struct supp_entry *entry = supp_find (fault_page);
  if (entry == NULL)
    return false;

  /* If the page is a file and not loaded, load it from file. */
  if (supp_is_not_loaded (entry) && supp_is_file (entry))
    {
      DEBUG_PRINT ("Found file entry for %p", fault_page);
      bool read_success = supp_load_file (entry);
      return read_success;
    }

  return false;
}

/* State and Type */

/* Convert state to string. */
static const char *
supp_state_to_string (enum supp_state state)
{
  switch (state)
    {
    case NOT_LOADED:
      return "NOT_LOADED";
    case LOADED:
      return "LOADED";
    case SWAPPED:
      return "SWAPPED";
    default:
      return "UNKNOWN";
    }
}

/* Convert type to string. */
static const char *
supp_type_to_string (enum supp_type type)
{
  switch (type)
    {
    case SUPP_NORMAL:
      return "NORMAL";
    case SUPP_CODE:
      return "FILE";
    case SUPP_ZERO:
      return "ZERO";
    case SUPP_MMAP:
      return "MMAP";
    default:
      return "UNKNOWN";
    }
}

/* Set state of ENTRY to STATE. */
static void
supp_set_state (struct supp_entry *entry, enum supp_state state)
{
  ASSERT (entry != NULL);
  ASSERT (entry->state != state);

  DEBUG_THREAD ("State %p: %s -> %s", entry->upage,
                supp_state_to_string (entry->state),
                supp_state_to_string (state));
  entry->state = state;
}

/* Return true if ENTRY has type of SUPP_CODE. */
bool
supp_is_code (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  return entry->type == SUPP_CODE;
}

/* Return true if ENTRY has type of SUPP_MMAP. */
bool
supp_is_mmap (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  return entry->type == SUPP_MMAP;
}

/* Return true if ENTRY has type of file. */
bool
supp_is_file (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  return supp_is_code (entry) || supp_is_mmap (entry);
}

/* Return true if ENTRY has type of SUPP_NORMAL. */
bool
supp_is_normal (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  return entry->type == SUPP_NORMAL;
}

/* Return true if ENTRY has type of SUPP_ZERO. */
bool
supp_is_zero (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  return entry->type == SUPP_ZERO;
}

/* Return true if ENTRY is LOADED. */
bool
supp_is_loaded (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  return entry->state == LOADED;
}

/* Return true if ENTRY is NOT_LOADED. */
bool
supp_is_not_loaded (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  return entry->state == NOT_LOADED;
}

/* Return true if ENTRY is SWAPPED. */
bool
supp_is_swapped (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  return entry->state == SWAPPED;
}

/* Return true if ENTRY is pinned. */
bool
supp_is_pinned (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  return entry->pinned;
}


/* Set kernel page of page entry ENTRY to KPAGE. */
void
supp_set_kpage (struct supp_entry *entry, void *kpage)
{
  ASSERT (entry != NULL);

  if (kpage == NULL)
    {
      ASSERT (entry->kpage != NULL);
    }
  else
    {
      ASSERT (entry->kpage == NULL);
    }

  entry->kpage = kpage;
}

/* Pagedir */

/* Return whether supplemental page ENTRY is dirty.

   An entry becomes dirty after it has been modified by the
   process. We determine this by checking the dirty bit in the
   supplemental page's pagedir, and the dirty value of entry.

   We do not check the dirty bit for kernel page, that means
   all modifications should use user pages. */
bool
supp_is_dirty (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  ASSERT (supp_is_loaded (entry));

  return entry->dirty
         || pagedir_is_dirty (entry->owner->pagedir, entry->upage);
}

/* Return whether supplemental page ENTRY is accessed.

   An entry becomes accessed after it has been read or written
   by the process. We determine this by checking the accessed bit
   in the supplemental page's pagedir.

   We do not check the accessed bit for kernel page. */
bool
supp_is_accessed (struct supp_entry *entry)
{
  ASSERT (entry != NULL);
  ASSERT (supp_is_loaded (entry));

  return pagedir_is_accessed (entry->owner->pagedir, entry->upage);
}

