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

#endif // VM_FRAME_H
