#include <stdio.h>
#include "libc_internal.h"

long ftell(FILE *fp) { return sys_seek(fp->fd, 0, SEEK_CUR); }
