#include <stdio.h>
#include <stdarg.h>

/*
 * fscanf-family core.  The string scanner (vsscanf) is reused by reading one
 * line from the stream into a buffer and parsing that.
 *
 * LIMITATION: input the format string does not consume is discarded together
 * with the remainder of the line (see docs/c99-conformance.md).  Programs that
 * read one item per line behave as expected.
 */
int vfscanf(FILE *fp, const char *fmt, va_list ap) {
  char line[512];
  int i = 0, c, got = 0;
  while (i < (int)sizeof(line) - 1) {
    c = fgetc(fp);
    if (c == EOF)
      break;
    got = 1;
    if (c == '\n')
      break; /* consume the newline, leave it out of the buffer */
    line[i++] = (char)c;
  }
  line[i] = '\0';
  if (!got)
    return EOF; /* input failure before any character was read */
  return vsscanf(line, fmt, ap);
}
