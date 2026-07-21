#include <string.h>
#include <errno.h>

char *strerror(int errnum) {
  switch (errnum) {
  case 0:
    return "Success";
  case ENOENT:
    return "No such file or directory";
  case EINVAL:
    return "Invalid argument";
  case EMFILE:
    return "Too many open files";
  default:
    return "Error";
  }
}
