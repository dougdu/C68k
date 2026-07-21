#include <stdlib.h>

/* qsort keeps its byte-swap helper private (shell sort; the size_t j >= gap
 * test guards the unsigned underflow). */
static void _swap(char *a, char *b, size_t size) {
  while (size--) {
    char t = *a;
    *a++ = *b;
    *b++ = t;
  }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*cmp)(const void *, const void *)) {
  char *a = (char *)base;
  for (size_t gap = nmemb / 2; gap > 0; gap /= 2) {
    for (size_t i = gap; i < nmemb; i++) {
      for (size_t j = i; j >= gap; j -= gap) {
        char *x = a + (j - gap) * size;
        char *y = a + j * size;
        if (cmp(x, y) <= 0)
          break;
        _swap(x, y, size);
      }
    }
  }
}
