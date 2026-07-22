#include <stdlib.h>
#include "libc_internal.h"

/* Terminate WITHOUT running atexit handlers or flushing streams. */
void _Exit(int code) {
  sys_exit(code);
  for (;;)
    ;
}
