#ifndef _STRINGS_H
#define _STRINGS_H

#include <stddef.h>

/* POSIX case-insensitive comparison (used by the tokenizer). */
int strncasecmp(const char *a, const char *b, size_t n);
int strcasecmp(const char *a, const char *b);

#endif /* _STRINGS_H */
