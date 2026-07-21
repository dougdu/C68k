#include <stdio.h>

int ferror(FILE *fp) { return (fp->flags & _SF_ERR) != 0; }
