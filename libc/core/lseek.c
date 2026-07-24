#include <unistd.h>
#include "libc_internal.h"

off_t lseek(int fd, off_t off, int whence) { return sys_seek(fd, off, whence); }
