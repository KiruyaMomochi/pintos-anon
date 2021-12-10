#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t)-1)

typedef int fd_t;
#define FD_ERROR ((fd_t)-1)

#include "threads/vaddr.h"

/* 8 MB of user stack. */
#define USER_STACK_SIZE (8 * 1024 * 1024)
#define USER_STACK_BOTTOM (PHYS_BASE - USER_STACK_SIZE)

#include "threads/synch.h"
#include "threads/thread.h"
#include "vm/page.h"
extern struct lock filesys_lock;

struct process
{
  struct thread *thread;
  char name[16];

  pid_t pid;     /* Process Id */
  int exit_code; /* Exit status. */

  struct file **fd_table; /* File descriptor table. */
  int fd_count;           /* Number of open files. */

  struct process *parent;      /* Parent process. */
  struct list chilren;         /* List of child processes. */
  struct list_elem child_elem; /* List element for children list. */

  bool load_success; /* Whether the process was loaded successfully. */

  struct semaphore load_sema; /* Semaphore for loading. */
  struct semaphore wait_sema; /* Semaphore for waiting. */
  struct semaphore exit_sema; /* Semaphore for exiting. */

  struct file *executable; /* Executable file. */

  struct supp_table supp_table; /* Supplemental page table. */
  void *esp; /* Stack pointer, used for save esp in syscall */
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void process_init (void);
const char *process_name (void);
struct process *process_current (void);
pid_t process_create (struct thread *t);
struct process *process_find (pid_t pid);

bool allocate_stack (void *upage, bool zero);

fd_t process_allocate_fd (struct file *);
struct file *process_get_file (fd_t);
void process_free_fd (fd_t fd);

bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                   uint32_t read_bytes, uint32_t zero_bytes, bool writable);

pid_t tid_to_pid (tid_t tid);
tid_t pid_to_tid (pid_t pid);

#endif /* userprog/process.h */
