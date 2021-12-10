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

