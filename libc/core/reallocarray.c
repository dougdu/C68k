#include <stdlib.h>
#include <errno.h>

/* realloc for an array, guarding against nmemb*size overflow. */
void *reallocarray(void *p, size_t nmemb, size_t size) {
  if (nmemb && size > (size_t)-1 / nmemb) {
    errno = ENOMEM;
    return 0;
  }
  return realloc(p, nmemb * size);
}
