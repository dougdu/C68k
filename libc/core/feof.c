#include <stdio.h>

int feof(FILE *fp) { return (fp->flags & _SF_EOF) != 0; }
