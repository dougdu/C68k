#include <string.h>

char *strsep(char **stringp, const char *delim) {
  char *start = *stringp;
  if (start == 0)
    return 0;
  for (char *p = start; *p; p++) {
    if (strchr(delim, (unsigned char)*p)) {
      *p = '\0';
      *stringp = p + 1;
      return start;
    }
  }
  *stringp = 0;
  return start;
}
