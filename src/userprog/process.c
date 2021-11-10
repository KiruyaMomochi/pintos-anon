#include "userprog/process.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include <debug.h>
#include <inttypes.h>
#include <kernel/debug.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static thread_func start_process NO_RETURN;
static bool load (char *cmdline, void (**eip) (void), void **esp);
static void init_process (struct process *p);

/* Convert thread indentifier to process identifier. 
   TODO: pid <-> tid */
pid_t
tid_to_pid (tid_t tid)
{
  ASSERT (tid != TID_ERROR);
  return tid;
}

/* Convert process identifier to thread indentifier. 
   TODO: pid <-> tid */
tid_t
pid_to_tid (pid_t pid)
{
  ASSERT (pid != PID_ERROR);
  return pid;
}


/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);

  struct thread *cur = thread_current ();
  struct thread *t = thread_find (tid);
  DEBUG_THREAD ("%d, %d", tid, t->tid);
  ASSERT (t != NULL);

  t->process->parent = cur->process;
  list_push_back (&(cur->process->chilren), &(t->process->child_elem));
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  struct thread *t = thread_current ();
  struct process *p = t->process;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  p->load_success = load (file_name, &if_.eip, &if_.esp);
  sema_up (&p->load_sema);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!p->load_success)
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid)
{
  // HACK: tid = pid so
  pid_t pid = tid_to_pid(child_tid);
  struct process *p = process_find (pid);
  if (p == NULL)
    return -1;

  sema_down (&p->wait_sema);
  int exit_code = p->exit_code;
  sema_up (&p->exit_sema);

  sema_down (&p->wait_sema);
  sema_up (&p->exit_sema);

  return exit_code;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *t = thread_current ();
  struct process *p = t->process;
  uint32_t *pd;

  /* Print the process's name and exit code. */
  printf ("%s: exit(%d)\n", p->name, p->exit_code);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = t->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      t->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  bool has_parent = p->parent != NULL;

  if (has_parent)
    {
      sema_up (&p->wait_sema);
      sema_down (&p->exit_sema);

      /* Remove the process from the parent's children list. */
      list_remove (&(p->child_elem));
    }

  /* Set children's parent to NULL */
  struct list_elem *e;
  for (e = list_begin (&p->chilren); e != list_end (&p->chilren);
       e = list_next (e))
    {
      struct process *child = list_entry (e, struct process, child_elem);
      child->parent = NULL;
    }

  /* Close all opened files */
  for (size_t fd = 2; fd < p->fd_count; fd++)
    {
      if (p->fd_table[fd] == NULL)
        continue;
      file_close (p->fd_table[fd]);
      process_free_fd (fd);
    }

  /* Free fd table */
  free (p->fd_table);

  file_close (p->executable);

  if (has_parent)
    {
      sema_up (&p->wait_sema);
      sema_down (&p->exit_sema);
    }

  /* Free the process's resources. */
  free (t->process);
  t->process = NULL;

  DEBUG_THREAD ("exit complete");
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

static int push_argv_arguments (void **esp, char *save_ptr);
static void push_argv (void **esp, char *program_name, char *strtok_ptr);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (char *file_name, void (**eip) (void), void **esp)
{
  /* Divide file name into words at spaces */
  char *save_ptr;
  char *program_name = strtok_r (file_name, " ", &save_ptr);
  ASSERT (program_name != NULL);

  struct thread *t = thread_current ();
  struct process *p = t->process;
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Set process name to program name */
  strlcpy (p->name, program_name, sizeof t->process->name);

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (program_name);

  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2
      || ehdr.e_machine != 3 || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr) || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *)mem_page, read_bytes,
                                 zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Push arguments. */
  push_argv (esp, program_name, save_ptr);

  /* Start address. */
  *eip = (void (*) (void))ehdr.e_entry;

  success = true;

  p->executable = filesys_open (program_name);
  file_deny_write (p->executable);

done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int)page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          palloc_free_page (kpage);
          return false;
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

static int
push_argv_arguments (void **esp, char *save_ptr)
{
  /* Find the next token. */
  char *token = strtok_r (NULL, " ", &save_ptr);
  char *argv = NULL;

  /* If we failed to get a token, we are at the end of argv. */
  if (token == NULL)
    {
      /* Round stack pointer down to a multiple of 4 */
      *esp -= (unsigned int)(*esp) % 4;

      /* Push null pointer to argv[argc] */
      *esp -= sizeof (uint8_t *);
      memcpy (*esp, &argv, sizeof (uint8_t *));

      return 0;
    }

  /* Push argv[i] */
  *esp -= strlen (token) + 1;
  strlcpy (*esp, token, strlen (token) + 1);
  argv = *esp;

  /* Recursively push the next token. */
  int argc = push_argv_arguments (esp, save_ptr) + 1;

  /* Push address of argv[i] */
  *esp -= sizeof (char *);
  memcpy (*esp, &argv, sizeof (char *));

  return argc;
}

static void
push_argv (void **esp, char *program_name, char *strtok_ptr)
{
  /* Push argv[0] */
  *esp -= strlen (program_name) + 1;
  strlcpy (*esp, program_name, strlen (program_name) + 1);
  void *argv = *esp;

  /* Push other arguments */
  int argc = push_argv_arguments (esp, strtok_ptr) + 1;

  /* Push address of argv[0] */
  *esp -= sizeof (char *);
  memcpy (*esp, &argv, sizeof (char *));
  argv = *esp;

  /* Push address of argv */
  *esp -= sizeof (void *);
  memcpy (*esp, &argv, sizeof (char **));

  /* Push argc */
  *esp -= sizeof (int);
  memcpy (*esp, &argc, sizeof (int));

  /* Push return address */
  *esp -= sizeof (void *);
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp)
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* Returns the process with ID pid in children, or NULL if no such thread
   exists. */
struct process *
process_find (pid_t pid)
{
  struct list_elem *e;

  struct process *cur = process_current ();
  ASSERT (cur != NULL);

  for (e = list_begin (&cur->chilren); e != list_end (&cur->chilren);
       e = list_next (e))
    {
      struct process *p = list_entry (e, struct process, child_elem);
      if (p->pid == pid)
        return p;
    }
  return NULL;
}

void
process_init (void)
{
  ASSERT (intr_get_level () == INTR_OFF);

  struct thread *t = thread_current ();
  struct process *p = process_create (t);
  if (p == NULL)
    PANIC ("Failed to init process");
}

/* Creates a new process. */
pid_t
process_create (struct thread *t)
{
  ASSERT (t != NULL);
  ASSERT (t->process == NULL);

  struct process *p = malloc (sizeof (struct process));
  if (p == NULL)
    return PID_ERROR;

  init_process (p);

  snprintf (p->name, sizeof (p->name), "[T]%s", t->name);
  t->process = p;
  p->pid = t->tid;

  return p->pid;
}

/* Does basic initialization of T as a process. */
static void
init_process (struct process *p)
{
  ASSERT (p != NULL);

  p->exit_code = -1;
  /* Initialize the file descriptor table. */
  p->fd_table = NULL;
  /* Set the initial file descriptor table size to 2 because
    stdin and stdout are reserved. */
  p->fd_count = 2;
  /* Initialize the thread children list. */
  list_init (&(p->chilren));
  sema_init (&(p->load_sema), 0);
  sema_init (&(p->wait_sema), 0);
  sema_init (&(p->exit_sema), 0);
  p->parent = NULL;
  p->executable = NULL;
}

/* Returns the name of the running process. */
const char *
process_name (void)
{
  return process_current ()->name;
}

/* Returns the running process. */
struct process *
process_current (void)
{
  struct process *p = thread_current ()->process;
  ASSERT (p != NULL);
  return p;
}

/* Allocate file descriptor for FILE. Returns -1 if failed. */
int
process_allocate_fd (struct file *file)
{
  struct process *p = process_current ();
  int fd;

  /* Find an unused file descriptor in current fd_table. */
  for (fd = 2; fd < p->fd_count; fd++)
    {
      if (p->fd_table[fd] == NULL)
        {
          p->fd_table[fd] = file;
          return fd;
        }
    }

  /* Try to extend fd_table if there is no unused fd. */
  ASSERT (fd == p->fd_count)
  int new_fd_count = p->fd_count * 2;
  ASSERT (new_fd_count > p->fd_count);
  struct file **new_fd_table
      = realloc (p->fd_table, new_fd_count * sizeof (struct file *));
  if (new_fd_table == NULL)
    return -1;
  memset (new_fd_table + p->fd_count, 0,
          (new_fd_count - p->fd_count) * sizeof (struct file *));
  p->fd_table = new_fd_table;
  p->fd_count = new_fd_count;

  /* Set file descriptor to new fd_table. */
  ASSERT (p->fd_table[fd] == NULL);
  p->fd_table[fd] = file;
  return fd;
}

/* Get file for FD. */
struct file *
process_get_file (int fd)
{
  struct process *p = process_current ();
  if (fd < 2 || fd >= p->fd_count)
    return NULL;
  // TODO: other cases?

  ASSERT (fd >= 2 && fd < p->fd_count);
  struct file *file = p->fd_table[fd];

  // ASSERT (file != NULL);
  return file;
}

/* Free file descriptor FD. */
void
process_free_fd (int fd)
{
  struct process *p = process_current ();
  ASSERT (fd >= 2 && fd < p->fd_count);
  ASSERT (p->fd_table[fd] != NULL);
  p->fd_table[fd] = NULL;
}
