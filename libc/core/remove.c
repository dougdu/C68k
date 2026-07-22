#include <stdio.h>
#include "libc_internal.h"

int remove(const char *path) { return sys_unlink(path); }
