#include <stdio.h>
#include "libc_internal.h"

/* Produce a filename that does not currently exist.  The name is 8.3-friendly
 * ("TMPnnnnn") so it is usable on CP/M as well as Osiris.  With s == NULL the
 * result points into a static buffer (overwritten by the next call). */
char *tmpnam(char *s) {
  static char sbuf[L_tmpnam];
  static unsigned seq;
  char *out = s ? s : sbuf;

  for (int tries = 0; tries < TMP_MAX; tries++) {
    unsigned n = ++seq;
    out[0] = 'T';
    out[1] = 'M';
    out[2] = 'P';
    for (int i = 7; i >= 3; i--) {
      out[i] = (char)('0' + n % 10);
      n /= 10;
    }
    out[8] = 0;
    int fd = sys_open(out, 0);
    if (fd < 0)
      return out; /* does not exist -> free to use */
    sys_close(fd); /* exists -> advance and retry */
  }
  return NULL;
}
