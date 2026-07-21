#include <libgen.h>
#include <string.h>

/* dirname(): may modify `path` (POSIX).  Handles both separators. */
char *dirname(char *path) {
  if (!path || !*path)
    return ".";
  char *end = path + strlen(path) - 1;
  while (end > path && (*end == '/' || *end == '\\'))
    *end-- = 0;
  char *slash = NULL;
  for (char *p = path; *p; p++)
    if (*p == '/' || *p == '\\')
      slash = p;
  if (!slash)
    return ".";
  if (slash == path) {
    path[1] = 0;
    return path;
  }
  *slash = 0;
  return path;
}
