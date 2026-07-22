/*
 * memtest.c -- c68k <stdlib.h> allocator conformance + stress battery.
 *
 * Exercises the libheap-backed malloc/free/calloc/realloc: correctness, the
 * edge cases (realloc(NULL,n), realloc(p,0), calloc overflow, malloc(0)), and
 * two stress passes -- a churn loop that allocates and frees far more than the
 * heap can hold at once (so it only completes because free() truly reclaims)
 * and an interleaved free/refill pass that exercises free-list reuse and
 * coalescing.  Self-checking: prints "MEM PASS n/n".  Name kept <= 8 chars
 * for CP/M 8.3.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int passes = 0;
static int total = 0;

static void chk(int cond, int id) {
  total++;
  if (cond)
    passes++;
  else
    printf("FAIL: check %d\n", id);
}

/* Fill a block with a position-dependent pattern and read it back. */
static int pattern_ok(unsigned char *p, size_t n, int seed) {
  size_t i;
  for (i = 0; i < n; i++)
    p[i] = (unsigned char)((i * 7 + seed) & 0xff);
  for (i = 0; i < n; i++)
    if (p[i] != (unsigned char)((i * 7 + seed) & 0xff))
      return 0;
  return 1;
}

int main(void) {
  /* --- malloc: non-NULL, writable, survives free --- */
  {
    unsigned char *p = (unsigned char *)malloc(100);
    chk(p != NULL, 1);
    chk(p && pattern_ok(p, 100, 3), 2);
    free(p);
  }

  /* --- calloc: zero-initialised --- */
  {
    int i, allzero = 1;
    unsigned char *c = (unsigned char *)calloc(50, 4);
    chk(c != NULL, 3);
    if (c)
      for (i = 0; i < 200; i++)
        if (c[i] != 0)
          allzero = 0;
    chk(allzero, 4);
    free(c);
  }

  /* --- realloc: grow preserves the leading bytes --- */
  {
    unsigned char *r = (unsigned char *)malloc(16);
    int i, kept = 1;
    if (r)
      for (i = 0; i < 16; i++)
        r[i] = (unsigned char)(i + 1);
    r = (unsigned char *)realloc(r, 256);
    chk(r != NULL, 5);
    if (r)
      for (i = 0; i < 16; i++)
        if (r[i] != (unsigned char)(i + 1))
          kept = 0;
    chk(kept, 6);
    if (r)
      chk(pattern_ok(r, 256, 9), 7);
    free(r);
  }

  /* --- realloc(NULL, n) behaves like malloc --- */
  {
    unsigned char *p = (unsigned char *)realloc(NULL, 40);
    chk(p != NULL, 8);
    chk(p && pattern_ok(p, 40, 1), 9);
    free(p);
  }

  /* --- free(NULL) is a no-op (must not crash) --- */
  free(NULL);
  chk(1, 10);

  /* --- calloc overflow returns NULL (nmemb*size wraps) --- */
  chk(calloc((size_t)-1, 2) == NULL, 11);

  /* --- realloc(p, 0) frees and returns NULL --- */
  {
    void *p = malloc(32);
    chk(realloc(p, 0) == NULL, 12);
  }

  /* --- malloc(0): a NULL or a unique freeable pointer, never a crash --- */
  {
    void *a = malloc(0);
    void *b = malloc(0);
    chk(a == NULL || a != b, 13);
    free(a);
    free(b);
  }

  /* --- reclamation churn: alloc+free far more than the heap holds --- */
  {
    int i, ok = 1;
    for (i = 0; i < 4000; i++) { /* 4000 * 4 KB = 16 MB >> any 68k heap */
      unsigned char *b = (unsigned char *)malloc(4096);
      if (!b) {
        ok = 0;
        break;
      }
      b[0] = (unsigned char)i; /* touch both ends to catch a short block */
      b[4095] = (unsigned char)~i;
      if (b[0] != (unsigned char)i || b[4095] != (unsigned char)~i) {
        ok = 0;
        break;
      }
      free(b); /* only completes if free() actually reclaims */
    }
    chk(ok, 14);
  }

  /* --- interleaved free/refill: exercise free-list reuse + coalescing --- */
  {
    void *v[64];
    int i, ok = 1;
    for (i = 0; i < 64; i++) {
      v[i] = malloc((size_t)(24 + i * 8)); /* mixed sizes across buckets */
      if (!v[i])
        ok = 0;
    }
    for (i = 0; i < 64; i += 2) { /* free the even blocks (leaves holes) */
      free(v[i]);
      v[i] = NULL;
    }
    for (i = 0; i < 64; i += 2) { /* refill from the holes */
      v[i] = malloc(96);
      if (!v[i])
        ok = 0;
    }
    for (i = 0; i < 64; i++) { /* every survivor still writable */
      if (v[i])
        *(char *)v[i] = (char)i;
    }
    for (i = 0; i < 64; i++)
      free(v[i]);
    chk(ok, 15);
  }

  printf("MEM PASS %d/%d\n", passes, total);
  return (passes == total) ? 0 : 1;
}
