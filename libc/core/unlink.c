#include <unistd.h>
#include "libc_internal.h"

int unlink(const char *path) { return sys_unlink(path); }
