#include <unistd.h>
#include "libc_internal.h"

ssize_t write(int fd, const void *buf, size_t n) {
  return sys_write(fd, buf, (int)n);
}
