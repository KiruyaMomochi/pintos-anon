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

