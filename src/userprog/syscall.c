#include "userprog/syscall.h"
#include "debug.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
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
        ret = exec (cmd_line); // TODO: Not finished
        DEBUG_PRINT_SYSCALL_END ("[%s (%s) -> %d]", syscall, cmd_line, ret);
        break;
      }
    case SYS_WAIT:
      {
        pid_t pid = *(sp + 1);
        DEBUG_PRINT_SYSCALL_START ("(%s (%d))", syscall, pid);
        ret = wait (pid); // TODO: Not finished
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
        DEBUG_PRINT_SYSCALL_START ("(%s (%s))", syscall, file);
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
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit ();
}
