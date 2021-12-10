#ifndef VM_PAGE_H
#define VM_PAGE_H

/* Supplemental page table and Frame table.

   The supplemental page table supplements the page table with
   additional data about each page. It is needed because of the
   limitations imposed by the page table's format. Such a data
   structure is often called a "page table" also. We add the word
   "supplemental" to reduce confusion.

   The frame table contains one entry for each frame that contains
   a user page. Each entry contains a pointer to the page, if any,
   that currently occupies it, and other data. The frame table allows
   us to efficiently implement an eviction policy, by choosing a
   page to evict when no frames are free.

   Each entry has three states and four types. An entry is NOT_LOADED
   at the beginning. After a page fault occurs or required by user,
   it's LOADED into the frame, and also in frame table.

   When the frame is full, some page maybe evicted. For a mmap'd file
   page, write it back to the file. For other pages, move the page
   to swap space, and it's state changes to SWAPPED.
*/

#include "filesys/file.h"
#include <hash.h>
#include <stdbool.h>
#include <stdint.h>

/* States of a page. */
enum supp_state
{
  NOT_LOADED, /* Not loaded into memory. */
  LOADED,     /* Loaded into memory. */
  SWAPPED,    /* Swapped out of memory. */
};

/* Types of a page. */
enum supp_type
{
  SUPP_NORMAL, /* A normal page. */
  SUPP_ZERO,   /* A normal page with all bytes zeroed. */
  SUPP_CODE,   /* Containing executable code. */
  SUPP_MMAP,   /* Containing a file mapped into memory. */
};

/* A supplemental page table entry, which is also an entry in the
   frame table. */
struct supp_entry
{
  enum supp_state state; /* state of the page */
  enum supp_type type;   /* type of the page */

  void *kpage; /* kernel virtual address of the page */
  void *upage; /* user virtual address of the page */
  struct thread *owner; /* thread that owns the page */

  bool writable;        /* is the page writable? */
  bool pinned;          /* is the page pinned? */
  bool dirty;           /* is the page dirty? */

  struct hash_elem supp_elem;  /* supplemental entry hash element */
  struct list_elem frame_elem; /* frame entry list element */


  /* For page containing file. */
  struct file *file; /* file containing the page */
  off_t ofs;         /* offset of the page in the file */
  size_t read_bytes; /* number of bytes read from the page */
  size_t zero_bytes; /* number of bytes zeroed in the page */
};

/* The supplemental page table. */
struct supp_table
{
  struct hash hash; /* hash table */
};

void supp_init (struct supp_table *table);
struct supp_entry *supp_find (void *upage);
void supp_unload (struct supp_entry *entry);
void supp_set_kpage (struct supp_entry *entry, void *kpage);

bool supp_handle_page_fault (void *);

/* Insertion and removal. */

struct supp_entry *supp_insert_code (void *upage, struct file *file, off_t ofs,
                                     size_t read_bytes, size_t zero_bytes,
                                     bool writable);
struct supp_entry *supp_insert_mmap (void *upage, struct file *file, off_t ofs,
                                     size_t read_bytes, size_t zero_bytes,
                                     bool writable);
struct supp_entry *supp_insert_stack (void *upage, bool zero, bool writable);

bool supp_destroy (void *upage);
void supp_remove_all (uint32_t *pd);

/* Property accessors. */

bool supp_is_code (struct supp_entry *entry);
bool supp_is_mmap (struct supp_entry *entry);
bool supp_is_file (struct supp_entry *entry);
bool supp_is_normal (struct supp_entry *entry);
bool supp_is_zero (struct supp_entry *entry);
bool supp_is_loaded (struct supp_entry *entry);
bool supp_is_not_loaded (struct supp_entry *entry);
bool supp_is_swapped (struct supp_entry *entry);
bool supp_is_pinned (struct supp_entry *entry);
bool supp_is_dirty (struct supp_entry *entry);
bool supp_is_accessed (struct supp_entry *entry);

#endif // VM_PAGE_H
