#include <stdio.h>
#include "libc_internal.h"

int fileno(FILE *fp) { return fp ? fp->fd : -1; }
