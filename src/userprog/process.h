#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t)-1)

#include "threads/thread.h"

struct process
{
  struct thread *thread;
  char name[16];

  pid_t pid;     /* Process Id */
  int exit_code; /* Exit status. */


  bool load_success; /* Whether the process was loaded successfully. */
  struct semaphore load_sema; /* Semaphore for loading. */

  struct file *executable; /* Executable file. */
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void process_init (void);
const char *process_name (void);

struct process *process_current (void);

pid_t process_create (struct thread *t);

#endif /* userprog/process.h */
