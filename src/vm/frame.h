#ifndef VM_FRAME_H
#define VM_FRAME_H

/* Frame table.
   The frame table is implemented as a linked list of pages.

   A frame entry is unified with a page table entry.
   Entries has a value `kpage`.
   
   `frame_allocate` will allocate a new page
   for entry, and set `kpage` to it. `frame_install` installs it into
   frame table. 

   `frame_free` will free the page of entry. Before freeing, we
   should uninstall it from frame table with `frame_uninstall`.
*/

#include "threads/palloc.h"
#include "threads/synch.h"
#include <list.h>
#include <stdbool.h>

#include "page.h"

void frame_init (void);

bool frame_allocate (enum palloc_flags flags, struct supp_entry *entry);
bool frame_allocate_swap (enum palloc_flags flags, struct supp_entry *entry);

void frame_remove (struct supp_entry *entry);

bool frame_install (struct supp_entry *entry);
void frame_uninstall (struct supp_entry *entry);
void frame_free (struct supp_entry *entry);

struct supp_entry *frame_lookup (const void *kpage);
void frame_print_table (void);

#endif // VM_FRAME_H
