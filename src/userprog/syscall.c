#include "userprog/syscall.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
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

#define DEBUG_SYSCALL 0
#ifdef DEBUG_SYSCALL
#define DEBUG_PRINT_SYSCALL_START(...) \
  printf (COLOR_HBLK); \
  printf (__VA_ARGS__); \
  printf (COLOR_RESET);
#define DEBUG_PRINT_SYSCALL_END(...) \
  printf (COLOR_CYN); \
  printf (__VA_ARGS__); \
  printf (COLOR_RESET "\n");
#define DEBUG_PRINT(...) printf (__VA_ARGS__)
#else
#define DEBUG_PRINT_SYSCALL_START(...)
#define DEBUG_PRINT_SYSCALL_END(...)
#define DEBUG_PRINT(...)
#endif

/* Registers handlers for system call. */
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  int *sp = f->esp;
  char *syscall_name;
  uint32_t ret;

  switch (*sp)
    {
    case SYS_HALT:
      syscall_name = "halt";
      break;
    case SYS_EXIT:
      syscall_name = "exit";
      break;
    case SYS_EXEC:
      syscall_name = "exec";
      break;
    case SYS_WAIT:
      syscall_name = "wait";
      break;
    case SYS_CREATE:
      syscall_name = "create";
      break;
    case SYS_REMOVE:
      syscall_name = "remove";
      break;
    case SYS_OPEN:
      syscall_name = "open";
      break;
    case SYS_FILESIZE:
      syscall_name = "filesize";
      break;
    case SYS_READ:
      syscall_name = "read";
      break;
    case SYS_WRITE:
      syscall_name = "write";
      break;
    case SYS_SEEK:
      syscall_name = "seek";
      break;
    case SYS_TELL:
      syscall_name = "tell";
      break;
    case SYS_CLOSE:
      syscall_name = "close";
      break;
    default:
      syscall_name = "unknown";
      break;
    }
  
  switch (*sp)
    {
    case SYS_HALT:
      {
        DEBUG_PRINT_SYSCALL_START ("(%s)", syscall_name);
        halt ();
        DEBUG_PRINT_SYSCALL_END ("[%s]", syscall_name);
        break;
      }
    case SYS_EXIT:
      {
        int status = *(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d))", syscall_name, status);
        exit (status);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d)]", syscall_name, status);
        break;
      }
    case SYS_EXEC:
      {
        char *cmd_line = (char *)*(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%s))", syscall_name, cmd_line);
        ret = exec (cmd_line); // TODO: Not finished
        DEBUG_PRINT_SYSCALL_END ("[%s (%s) -> %d]", syscall_name, cmd_line, ret);
        break;
      }
    case SYS_WAIT:
      {
        pid_t pid = *(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d))", syscall_name, pid);
        ret = wait (pid); // TODO: Not finished
        DEBUG_PRINT_SYSCALL_END ("[%s (%d) -> %d]", syscall_name, pid, ret);
        break;
      }
    case SYS_CREATE:
      {
        char *file = (char *)*(sp + 1);
        unsigned initial_size = *(sp + 2);
        DEBUG_PRINT_SYSCALL_START ("(%s (%s, %d))", syscall_name, file, initial_size);
        ret = create (file, initial_size);
        DEBUG_PRINT_SYSCALL_END ("[%s (%s, %d) -> %d]", syscall_name, file, initial_size, ret);
        break;
      }
    case SYS_REMOVE:
      {
        char *file = (char *)*(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%s))", syscall_name, file);
        ret = remove (file);
        DEBUG_PRINT_SYSCALL_END ("[%s (%s) -> %d]", syscall_name, file, ret);
        break;
      }
    case SYS_OPEN:
      {
        char *file = (char *)*(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%s))", syscall_name, file);
        ret = open (file);
        DEBUG_PRINT_SYSCALL_END ("[%s (%s) -> %d]", syscall_name, file, ret);
        break;
      }
    case SYS_FILESIZE:
      {
        int fd = *(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d))", syscall_name, fd);
        ret = filesize (fd);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d) -> %d]", syscall_name, fd, ret);
        break;
      }
    case SYS_READ:
      {
        int fd = *(sp + 1);
        void *buffer = (void *)*(sp + 2);
        unsigned size = *(sp + 3);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d, %p, %d))", syscall_name, fd, buffer, size);
        ret = read (fd, buffer, size);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d, %p, %d) -> %d]", syscall_name, fd, buffer, size, ret);
        break;
      }
    case SYS_WRITE:
      {
        int fd = *(sp + 1);
        const void *buffer = (const void *)*(sp + 2);
        unsigned size = *(sp + 3);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d, %p, %d))", syscall_name, fd, buffer, size);
        ret = write (fd, buffer, size);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d, %p, %d) -> %d]", syscall_name, fd, buffer, size, ret);
        break;
      }
    case SYS_SEEK:
      {
        int fd = *(sp + 1);
        unsigned position = *(sp + 2);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d, %d))", syscall_name, fd, position);
        seek (fd, position);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d, %d)]", syscall_name, fd, position);
        break;
      }
    case SYS_TELL:
      {
        int fd = *(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d))", syscall_name, fd);
        ret = tell (fd);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d) -> %d]", syscall_name, fd, ret);
        break;
      }
    case SYS_CLOSE:
      {
        int fd = *(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d))", syscall_name, fd);
        close (fd);
        DEBUG_PRINT_SYSCALL_END ("[%s (%d)]", syscall_name, fd);
        break;
      }
    default:
      // TODO: Handle unknown system call
      ret = -1;
      break;
    }

  f->eax = ret;
  // thread_exit ();
}

/* Convert thread indentifier to process identifier. */
static pid_t
tid_to_pid (tid_t tid)
{
  ASSERT (tid != TID_ERROR);
  // TODO: Not finished
  return tid;
}

/* Convert process identifier to thread indentifier. */
static tid_t
pid_to_tid (pid_t pid)
{
  ASSERT (pid != PID_ERROR);
  // TODO: Not finished
  return pid;
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
  struct thread *cur = thread_current ();
  cur->exit_code = status;

  /* Exit the current thread. */
  thread_exit ();
}

/* Runs the executable whose name is given in CMD_LINE, passing any given
   arguments, and returns the new process's program id (pid). If the program
   cannot load or run for any reason, returns -1. */
static pid_t
exec (const char *cmd_line)
{
  // TODO: Wait until we know if child process is successfully loaded.
  // TODO: Check memory access.
  tid_t tid = process_execute (cmd_line);
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
  // TODO: Not implemented yet.
  tid_t tid = pid_to_tid (pid);
  return process_wait (tid);
}

/* Creates a file called FILE initially INITIAL_SIZE bytes in size.
   Returns true if successful, false otherwise. Creating a file does not
   open it. To open a file, use open(). */
static bool
create (const char *file, unsigned initial_size)
{
  // TODO: Check memory access.
  return filesys_create (file, initial_size);
}

/* Deletes the file called FILE. Returns true if successful, false
   otherwise. A file may be removed regardless of whether it is open or
   closed, and removing an open file does not close it. */
static bool
remove (const char *file)
{
  // TODO: Check memory access.
  return filesys_remove (file);
}

/* Opens the file called FILE. Returns a nonnegative integer handle
   called a "file descriptor" (fd), or -1 if the file could not be
   opened. */
static int
open (const char *file)
{
  // TODO: Check memory access.
  struct file *f = filesys_open (file);
  if (f == NULL)
    return -1;
  int fd = thread_allocate_fd (f);
  return fd;
}

/* Returns the size, in bytes, of the file open as FS. */
static int
filesize (int fd)
{
  struct file *f = thread_get_file (fd);
  return file_length (f);
}

/* Reads SIZE bytes from the file open as FD into BUFFER. Returns the
   number of bytes actually read (0 at end of file), or -1 if the file
   could not be read (due to a condition other than end of file). */
static int
read (int fd, void *buffer, unsigned size)
{
  // TODO: Check memory access.
  if (fd == STDIN_FILENO)
    {
      return read_stdin (buffer, size);
    }
  else
    {
      struct file *f = thread_get_file (fd);
      return file_read (f, buffer, size);
    }
}

/* Writes SIZE bytes from BUFFER to the open file FD. Returns the number
   of bytes actually written, which may be less than SIZE if some bytes
   could not be written. */
static int
write (int fd, const void *buffer, unsigned size)
{
  // TODO: Check memory access.
  if (fd == STDOUT_FILENO)
    {
      return write_stdout (buffer, size);
    }
  else
    {
      struct file *f = thread_get_file (fd);
      return file_write (f, buffer, size);
    }
}

/* Changes the next byte to be read or written in open file FD to
   position POSITION, expressed as a byte offset from the beginning of
   the file. (Thus, a position of 0 is the file's start.) */
static void
seek (int fd, unsigned position)
{
  struct file *f = thread_get_file (fd);
  file_seek (f, position);
}

/* Returns the position of the next byte to be read or written in open
   file FD, expressed in bytes from the beginning of the file. */
static unsigned
tell (int fd)
{
  struct file *f = thread_get_file (fd);
  return file_tell (f);
}

/* Closes file descriptor FD. */
static void
close (int fd)
{
  struct file *f = thread_get_file (fd);
  file_close (f);
  thread_free_fd (fd);
}
