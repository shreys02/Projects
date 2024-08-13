/* Tries to create a directory with the same name as an existing
   file, which must return failure. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  CHECK (create ("abc", 0), "create \"abc\"");
  // bool success = mkdir ("abc");
  // msg("This is sucess %d\n", success);
  CHECK (!mkdir ("abc"), "mkdir \"abc\" (must return false)");
}
