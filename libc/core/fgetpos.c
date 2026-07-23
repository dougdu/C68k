#include <stdio.h>

/* `fpos_t` is a plain byte offset (== ftell/fseek), so these are thin wrappers
 * over ftell/fseek. */
int fgetpos(FILE *fp, fpos_t *pos) {
  long t = ftell(fp);
  if (t < 0)
    return -1;
  *pos = t;
  return 0;
}

int fsetpos(FILE *fp, const fpos_t *pos) {
  return fseek(fp, (long)*pos, SEEK_SET);
}
