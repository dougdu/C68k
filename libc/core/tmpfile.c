#include <stdio.h>
#include <string.h>
#include "libc_internal.h"

/* Create a temporary file open for update ("wb+") that is deleted when it is
 * closed -- the _SF_TMP flag makes fclose unlink tmpname. */
FILE *tmpfile(void) {
  char name[L_tmpnam];
  if (!tmpnam(name))
    return NULL;
  FILE *fp = fopen(name, "wb+");
  if (!fp)
    return NULL;
  strcpy(fp->tmpname, name);
  fp->flags |= _SF_TMP;
  return fp;
}
