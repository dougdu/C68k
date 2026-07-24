#include <unistd.h>
#include "libc_internal.h"

ssize_t read(int fd, void *buf, size_t n) { return sys_read(fd, buf, (int)n); }
