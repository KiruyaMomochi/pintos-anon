#include "userprog/syscall.h"
#include "debug.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "kernel/debug.h"
#include "pagedir.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "utils/colors.h"
#include <stdio.h>
#include <syscall-nr.h>

static void syscall_handler (struct intr_frame *);
static void halt (void) NO_RETURN;
static void exit (int status) NO_RETURN;
static pid_t exec (const char *file);
static int wait (pid_t);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned size);
static int write (int fd, const void *buffer, unsigned size);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);

#ifdef DEBUG_KERNEL
#define DEBUG_PRINT_SYSCALL_START(...)                                        \
  printf (COLOR_HBLK);                                                        \
  printf (__VA_ARGS__);                                                       \
  printf (COLOR_RESET);
#define DEBUG_PRINT_SYSCALL_END(...)                                          \
  printf (COLOR_CYN);                                                         \
  printf (__VA_ARGS__);                                                       \
  printf (COLOR_RESET "\n");
#else
#define DEBUG_PRINT_SYSCALL_START(...)
#define DEBUG_PRINT_SYSCALL_END(...)
#endif

/* Registers handlers for system call. */
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm("movl $1f, %0; movzbl %1, %0; 1:" : "=&a"(result) : "m"(*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm("movl $1f, %0; movb %b2, %1; 1:"
      : "=&a"(error_code), "=m"(*udst)
      : "q"(byte));
  return error_code != -1;
}

/* Check if the address is invalid. */
static bool
is_invalid_address (const void *address)
{
  bool valid = address != NULL && is_user_vaddr (address);
  if (valid && get_user ((const uint8_t *)address) == -1)
    valid = false;

  return !valid;
}

/* Exit if the address is invalid. */
static void
check_address (const void *address)
{
  if (!is_invalid_address (address))
    return;

  DEBUG_PRINT (COLOR_HRED "Invalid address: %p", address);
  exit (-1);
}

/* Exit if the string is invalid. */
static void
check_string (const char *string)
{
  check_address (string);
  while (*string != '\0')
    check_address (++string);
}

/* Name of system call. */
static const char *
syscall_name (int syscall_number)
{
  switch (syscall_number)
    {
    case SYS_HALT:
      return "halt";
    case SYS_EXIT:
      return "exit";
    case SYS_EXEC:
      return "exec";
    case SYS_WAIT:
      return "wait";
    case SYS_CREATE:
      return "create";
    case SYS_REMOVE:
      return "remove";
    case SYS_OPEN:
      return "open";
    case SYS_FILESIZE:
      return "filesize";
    case SYS_READ:
      return "read";
    case SYS_WRITE:
      return "write";
    case SYS_SEEK:
      return "seek";
    case SYS_TELL:
      return "tell";
    case SYS_CLOSE:
      return "close";
    case SYS_MMAP:
      return "mmap";
    case SYS_MUNMAP:
      return "munmap";
    case SYS_CHDIR:
      return "chdir";
    case SYS_MKDIR:
      return "mkdir";
    case SYS_READDIR:
      return "readdir";
    case SYS_ISDIR:
      return "isdir";
    case SYS_INUMBER:
      return "inumber";
    default:
      return "unknown";
    }
}

/* Number of arguments of system call. */
static size_t
syscall_argc (int syscall_number)
{
  switch (syscall_number)
    {
    case SYS_HALT:
      return 0;
    case SYS_EXIT:
      return 1;
    case SYS_EXEC:
      return 1;
    case SYS_WAIT:
      return 1;
    case SYS_CREATE:
      return 2;
    case SYS_REMOVE:
      return 1;
    case SYS_OPEN:
      return 1;
    case SYS_FILESIZE:
      return 1;
    case SYS_READ:
      return 3;
    case SYS_WRITE:
      return 3;
    case SYS_SEEK:
      return 2;
    case SYS_TELL:
      return 1;
    case SYS_CLOSE:
      return 1;
    case SYS_MMAP:
      return 2;
    case SYS_MUNMAP:
      return 1;
    case SYS_CHDIR:
      return 1;
    case SYS_MKDIR:
      return 1;
    case SYS_READDIR:
      return 2;
    case SYS_ISDIR:
      return 1;
    case SYS_INUMBER:
      return 1;
    default:
      return 0;
    }
}

/* Exit if the system call sp or the argument is invalid. */
static void
check_sp_and_arg (int *sp)
{
  void *arg = (void *)sp;

  for (size_t i = 0; i < sizeof (sp); i++)
    check_address (++arg);

  size_t arg_size = syscall_argc (get_user ((const uint8_t *)sp));
  // DEBUG_THREAD ("%p %d", arg, arg_size);

  for (size_t i = 0; i < arg_size; i++)
    check_address (++arg);
}

/* Handle syscals and also check if inputs are correct */
static void
syscall_handler (struct intr_frame *f)
{
  int *sp = f->esp;
  uint32_t ret = 0;

  check_sp_and_arg (sp);
  const char *syscall = syscall_name (*sp);

  switch (*sp)
    {
    case SYS_HALT:
      {
        DEBUG_PRINT_SYSCALL_START ("(%s)", syscall);
        halt ();
        DEBUG_PRINT_SYSCALL_END ("[%s]", syscall);
        break;
      }
    case SYS_EXIT:
      {
        int status = *(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d))", syscall, status);
        exit (status);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d)]", syscall, status);
        break;
      }
    case SYS_EXEC:
      {
        char *cmd_line = (char *)*(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%s))", syscall, cmd_line);
        ret = exec (cmd_line);
        DEBUG_PRINT_SYSCALL_END ("[%s (%s) -> %d]", syscall, cmd_line, ret);
        break;
      }
    case SYS_WAIT:
      {
        pid_t pid = *(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d))", syscall, pid);
        ret = wait (pid);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d) -> %d]", syscall, pid, ret);
        break;
      }
    case SYS_CREATE:
      {
        char *file = (char *)*(sp + 1);
        unsigned initial_size = *(sp + 2);
        DEBUG_PRINT_SYSCALL_START ("(%s (%s, %d))", syscall, file,
                                   initial_size);
        ret = create (file, initial_size);
        DEBUG_PRINT_SYSCALL_END ("[%s (%s, %d) -> %d]", syscall, file,
                                 initial_size, ret);
        break;
      }
    case SYS_REMOVE:
      {
        char *file = (char *)*(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%s))", syscall, file);
        ret = remove (file);
        DEBUG_PRINT_SYSCALL_END ("[%s (%s) -> %d]", syscall, file, ret);
        break;
      }
    case SYS_OPEN:
      {
        char *file = (char *)*(sp + 1);
        ret = open (file);
        DEBUG_PRINT_SYSCALL_END ("[%s (%s) -> %d]", syscall, file, ret);
        break;
      }
    case SYS_FILESIZE:
      {
        int fd = *(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d))", syscall, fd);
        ret = filesize (fd);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d) -> %d]", syscall, fd, ret);
        break;
      }
    case SYS_READ:
      {
        int fd = *(sp + 1);
        void *buffer = (void *)*(sp + 2);
        unsigned size = *(sp + 3);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d, %p, %d))", syscall, fd, buffer,
                                   size);
        ret = read (fd, buffer, size);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d, %p, %d) -> %d]", syscall, fd,
                                 buffer, size, ret);
        break;
      }
    case SYS_WRITE:
      {
        int fd = *(sp + 1);
        const void *buffer = (const void *)*(sp + 2);
        unsigned size = *(sp + 3);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d, %p, %d))", syscall, fd, buffer,
                                   size);
        ret = write (fd, buffer, size);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d, %p, %d) -> %d]", syscall, fd,
                                 buffer, size, ret);
        break;
      }
    case SYS_SEEK:
      {
        int fd = *(sp + 1);
        unsigned position = *(sp + 2);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d, %d))", syscall, fd, position);
        seek (fd, position);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d, %d)]", syscall, fd, position);
        break;
      }
    case SYS_TELL:
      {
        int fd = *(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d))", syscall, fd);
        ret = tell (fd);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d) -> %d]", syscall, fd, ret);
        break;
      }
    case SYS_CLOSE:
      {
        int fd = *(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d))", syscall, fd);
        close (fd);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d)]", syscall, fd);
        break;
      }
    default:
      PANIC (COLOR_RED "Unknown system call %s" COLOR_RESET, syscall);
      ret = -1;
      break;
    }

  f->eax = ret;
  // thread_exit ();
}

/* Terminates Pintos by calling shutdown_power_off() (declared in
   "threads/init.h"). This should be seldom used, because you lose
   the ability to shut down the system. */
static void
halt (void)
{
  shutdown_power_off ();
}

/* Reads SIZE bytes from the keyboard using input_getc() into BUFFER.
   Returns the number of bytes actually read. */
static int
read_stdin (void *buffer, unsigned size)
{
  char *buf = buffer;
  for (unsigned i = 0; i < size; i++)
    {
      check_address (buf);
      buf[i] = input_getc ();
    }
  return size;
}

/* Writes all SIZE bytes from BUFFER to the console. */
static int
write_stdout (const void *buffer, unsigned size)
{
  const char *buf = buffer;
  for (; size >= WRITE_BUF_SIZE; size -= WRITE_BUF_SIZE)
    {
      check_address (buf);
      putbuf (buf, WRITE_BUF_SIZE);
      buf += WRITE_BUF_SIZE;
    }
  putbuf (buf, size);

  return size;
}

/* Terminates the current user program, returning STATUS to the kernel.
   If the process's parent waits for it (see below), this is the status
   that will be returned. Conventionally, a status of 0 indicates success
   and nonzero values indicate errors. */
static void
exit (int status)
{
  /* Set the exit status of the current thread. */
  struct process *p = process_current ();
  p->exit_code = status;

  /* Exit the current thread. */
  thread_exit ();
}

/* Runs the executable whose name is given in CMD_LINE, passing any given
   arguments, and returns the new process's program id (pid). If the program
   cannot load or run for any reason, returns -1. */
static pid_t
exec (const char *cmd_line)
{
  check_string (cmd_line);

  tid_t tid = process_execute (cmd_line);
  if (tid == TID_ERROR)
    return PID_ERROR;

  struct thread *t = thread_find (tid);
  struct process *p = t->process;
  if (p == NULL)
    return PID_ERROR;

  sema_down (&p->load_sema);
  if (!p->load_success)
    {
      sema_up (&p->exit_sema);
      sema_up (&p->exit_sema);
      sema_down (&p->wait_sema);
      sema_down (&p->wait_sema);
      return PID_ERROR;
    }

  pid_t pid = tid_to_pid (tid);
  return pid;
}

/* Waits for a child process PID and retrieves the child's exit status.
   If PID is still alive, waits until it terminates. Then, returns the
   status that pid passed to exit. If pid did not call exit(), but was
   terminated by the kernel (e.g. killed due to an exception), wait(pid)
   returns -1. */
static int
wait (pid_t pid)
{
  if (pid == PID_ERROR)
    return -1;

  return process_wait (pid);
}

/* Creates a file called FILE initially INITIAL_SIZE bytes in size.
   Returns true if successful, false otherwise. Creating a file does not
   open it. To open a file, use open(). */
static bool
create (const char *file, unsigned initial_size)
{
  check_string (file);

  return filesys_create (file, initial_size);
}

/* Deletes the file called FILE. Returns true if successful, false
   otherwise. A file may be removed regardless of whether it is open or
   closed, and removing an open file does not close it. */
static bool
remove (const char *file)
{
  check_string (file);

  return filesys_remove (file);
}

/* Opens the file called FILE. Returns a nonnegative integer handle
   called a "file descriptor" (fd), or -1 if the file could not be
   opened. */
static int
open (const char *file)
{
  check_string (file);
  DEBUG_PRINT_SYSCALL_START ("(%s (%s))", "open", file);

  struct file *f = filesys_open (file);
  if (f == NULL)
    return -1;
  int fd = process_allocate_fd (f);
  return fd;
}

/* Returns the size, in bytes, of the file open as FS. */
static int
filesize (int fd)
{
  struct file *f = process_get_file (fd);
  return file_length (f);
}

/* Reads SIZE bytes from the file open as FD into BUFFER. Returns the
   number of bytes actually read (0 at end of file), or -1 if the file
   could not be read (due to a condition other than end of file). */
static int
read (int fd, void *buffer, unsigned size)
{
  check_address (buffer);

  if (fd == STDIN_FILENO)
    {
      return read_stdin (buffer, size);
    }
  else
    {
      struct file *f = process_get_file (fd);
      if (f == NULL)
        {
          DEBUG_PRINT_SYSCALL_END (COLOR_HRED "[open %d failed]", fd);
          thread_exit ();
        }
      return file_read (f, buffer, size);
    }
}

/* Writes SIZE bytes from BUFFER to the open file FD. Returns the number
   of bytes actually written, which may be less than SIZE if some bytes
   could not be written. */
static int
write (int fd, const void *buffer, unsigned size)
{
  int write_size;
  check_address (buffer);

  // TODO: Use synchronization to ensure that only one thread can write to
  //       the same file at a time.
  // TODO: After add synchronization, we need to release the lock when
  //       the write is not done and process exit abnormally.
  struct process* p = process_current();
  sema_up(&(p->rw_sema));
  if (fd == STDOUT_FILENO)
    {
      write_size = write_stdout (buffer, size);
    }
  else
    {
      struct file *f = process_get_file (fd);
      if (f == NULL)
        {
          DEBUG_PRINT_SYSCALL_END (COLOR_HRED "[open %d failed]", fd);
          thread_exit ();
        }
      write_size = file_write (f, buffer, size);
    }
  sema_down(&(p->rw_sema));
  return write_size;
}

/* Changes the next byte to be read or written in open file FD to
   position POSITION, expressed as a byte offset from the beginning of
   the file. (Thus, a position of 0 is the file's start.) */
static void
seek (int fd, unsigned position)
{
  struct file *f = process_get_file (fd);
  file_seek (f, position);
}

/* Returns the position of the next byte to be read or written in open
   file FD, expressed in bytes from the beginning of the file. */
static unsigned
tell (int fd)
{
  struct file *f = process_get_file (fd);
  return file_tell (f);
}

/* Closes file descriptor FD. */
static void
close (int fd)
{
  struct file *f = process_get_file (fd);
  if (f == NULL)
    {
      DEBUG_PRINT_SYSCALL_END (COLOR_HRED "[close %d failed]", fd);
      thread_exit ();
    }
  file_close (f);
  process_free_fd (fd);
}
