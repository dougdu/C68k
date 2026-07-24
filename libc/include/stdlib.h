#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

typedef struct {
  int quot, rem;
} div_t;
typedef struct {
  long quot, rem;
} ldiv_t;
typedef struct {
  long long quot, rem;
} lldiv_t;

#define RAND_MAX 32767

void *malloc(size_t n);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *p, size_t n);
void *reallocarray(void *p, size_t nmemb, size_t size);
void *aligned_alloc(size_t alignment, size_t size);
int posix_memalign(void **memptr, size_t alignment, size_t size);
void free(void *p);

int atoi(const char *s);
long atol(const char *s);
long long atoll(const char *s);
double atof(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
double strtod(const char *s, char **end);
float strtof(const char *s, char **end);
long double strtold(const char *s, char **end);

int abs(int n);
long labs(long n);
long long llabs(long long n);
div_t div(int num, int den);
ldiv_t ldiv(long num, long den);
lldiv_t lldiv(long long num, long long den);

int rand(void);
void srand(unsigned seed);

void qsort(void *base, size_t nmemb, size_t size,
           int (*cmp)(const void *, const void *));
void qsort_r(void *base, size_t nmemb, size_t size,
             int (*cmp)(const void *, const void *, void *), void *arg);
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *));

void exit(int code);
int atexit(void (*fn)(void));
int at_quick_exit(void (*fn)(void));
void quick_exit(int code);
void abort(void);
void _Exit(int code);
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *string);
int clearenv(void);
int system(const char *command);

/* Non-standard but ubiquitous integer->string helpers (base 2..36; base 10
 * treats the value as signed, other bases as unsigned). */
char *itoa(int value, char *buf, int base);
char *utoa(unsigned value, char *buf, int base);
char *ltoa(long value, char *buf, int base);
char *ultoa(unsigned long value, char *buf, int base);

/* BSD program-name accessors (used by <err.h>). */
const char *getprogname(void);
void setprogname(const char *name);

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#endif /* _STDLIB_H */
