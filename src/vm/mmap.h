#ifndef VM_MMAP_H
#define VM_MMAP_H

#include "filesys/file.h"
#include <stddef.h>
#include <stdbool.h>

struct mmap_file
{
  struct file *file;
  void *uaddr;
  size_t page_cnt;
};

struct mmap_file *mmap_file_create (struct file *file, void *uaddr);
bool mmap_file_destroy (struct mmap_file *mmap_file);

#endif // VM_MMAP_H
