#include <stdio.h>

void rewind(FILE *fp) {
  fseek(fp, 0L, SEEK_SET);
  fp->flags &= ~(_SF_EOF | _SF_ERR);
}
