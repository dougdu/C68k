#include <stdlib.h>

/* No native single-precision string parse; narrow the double result. */
float strtof(const char *s, char **end) { return (float)strtod(s, end); }
