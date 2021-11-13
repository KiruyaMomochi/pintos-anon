/* Wait for a subprocess to finish, twice.
   The first call must wait in the usual way and return the exit code.
   The second wait call must return -1 immediately. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  pid_t child_1 = exec ("child-simple");
  msg ("wait(exec()) = %d", wait (child_1));
  msg ("wait(exec()) = %d", wait (child_1));
}
