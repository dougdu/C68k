#include <unistd.h>
#include "libc_internal.h"

int close(int fd) { return sys_close(fd); }
