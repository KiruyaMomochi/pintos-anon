#include "page.h"
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

