#include <stdio.h>

void clearerr(FILE *fp) { fp->flags &= ~(_SF_EOF | _SF_ERR); }
