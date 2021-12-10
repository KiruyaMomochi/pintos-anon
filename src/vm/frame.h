#ifndef VM_FRAME_H
#define VM_FRAME_H

/* Frame table.
   The frame table is implemented as a linked list of pages.
*/

#include "threads/palloc.h"
#include <list.h>
#include <stdbool.h>

#include "page.h"

void frame_init (void);

bool frame_allocate (enum palloc_flags flags, struct supp_entry *entry);

void frame_remove (struct supp_entry *entry);

bool frame_install (struct supp_entry *entry);
void frame_uninstall (struct supp_entry *entry);
void frame_free (struct supp_entry *entry);

struct supp_entry *frame_lookup (const void *kpage);
void frame_print_table (void);

#endif // VM_FRAME_H
