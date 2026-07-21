#include <libgen.h>
#include <string.h>

/* basename(): may modify `path` (POSIX).  Handles both separators. */
char *basename(char *path) {
  if (!path || !*path)
    return ".";
  char *end = path + strlen(path) - 1;
  while (end > path && (*end == '/' || *end == '\\'))
    *end-- = 0;
  char *slash = NULL;
  for (char *p = path; *p; p++)
    if (*p == '/' || *p == '\\')
      slash = p;
  return slash ? slash + 1 : path;
}
