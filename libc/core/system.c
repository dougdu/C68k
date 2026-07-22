#include <stdlib.h>

/* No command processor: system(NULL) reports "none available" (0); any real
 * command request fails (-1). */
int system(const char *command) { return command ? -1 : 0; }
