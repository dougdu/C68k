#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

void *malloc(size_t n);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);

int atoi(const char *s);
long atol(const char *s);
long strtol(const char *s, char **end, int base);

int abs(int n);

void exit(int code);
void abort(void);

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#endif /* _STDLIB_H */
