#include <string.h>
#include "libc_internal.h"

/* Collation follows LC_COLLATE.  _coll_weights is NULL in the "C"/"POSIX"
 * locale (and wherever no OS collating table exists, e.g. CP/M-68K) -> strcoll
 * is a plain byte comparison and strxfrm an identity copy.  In the native ("")
 * locale on Osiris, setlocale loads a 256-entry weight table (the OS 65h AL=6
 * collating sequence) here; strcoll then compares weights and strxfrm maps each
 * byte to its weight so strcmp on the keys matches strcoll.  Defined here, in
 * the consumer, so a strcoll-only program stays self-contained (a setlocale
 * that never touches strcoll does not force byte-order either way). */
const unsigned char *_coll_weights = 0;

int strcoll(const char *a, const char *b) {
  const unsigned char *w = _coll_weights;
  if (!w)
    return strcmp(a, b);
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  while (*pa && w[*pa] == w[*pb]) { /* raw byte guards termination, not weight */
    pa++;
    pb++;
  }
  return (int)w[*pa] - (int)w[*pb];
}

size_t strxfrm(char *dst, const char *src, size_t n) {
  const unsigned char *w = _coll_weights;
  size_t len = strlen(src);
  if (n) {
    size_t i = 0;
    for (; i < n - 1 && src[i]; i++)
      dst[i] = w ? (char)w[(unsigned char)src[i]] : src[i];
    dst[i] = 0;
  }
  return len;
}
