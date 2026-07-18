/*
 * libtest.c -- c68k C standard library conformance battery.
 *
 * Exercises <stdlib.h> (strtol/strtoul/atoi/atof/strtod, abs/labs,
 * div/ldiv, rand/srand, qsort/bsearch), <stdio.h> sscanf, and a
 * cross-section of <string.h>. Self-checking: prints "LIB PASS n/n".
 * Names kept <= 8 chars for CP/M 8.3 (LIBTEST.PRG / LIBTEST.68K).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static int passes = 0;
static int total = 0;
static volatile int sig_hit = 0;

static void onsig(int s) {
  (void)s;
  sig_hit = 1;
}

static void chk(int cond, int id) {
  total++;
  if (cond)
    passes++;
  else
    printf("FAIL: check %d\n", id);
}

static int icmp(const void *a, const void *b) {
  int x = *(const int *)a;
  int y = *(const int *)b;
  return (x > y) - (x < y);
}

static int dclose(double a, double b) {
  double d = a - b;
  if (d < 0)
    d = -d;
  return d < 1e-6;
}

int main(void) {
  /* --- strtol / strtoul --- */
  chk(strtol("ff", 0, 16) == 255, 1);
  chk(strtol("-42", 0, 10) == -42, 2);
  chk(strtoul("0x1A", 0, 0) == 26, 3);
  chk(strtoul("777", 0, 8) == 511, 4);

  /* --- atoi / atol / atof / strtod --- */
  chk(atoi("123") == 123, 5);
  chk(atol("-7") == -7, 6);
  chk(dclose(atof("2.5"), 2.5), 7);
  {
    char *e;
    double v = strtod("3.14xyz", &e);
    chk(dclose(v, 3.14) && *e == 'x', 8);
  }

  /* --- abs / labs / div / ldiv --- */
  chk(abs(-5) == 5, 9);
  chk(labs(-100000L) == 100000L, 10);
  {
    div_t d = div(17, 5);
    chk(d.quot == 3 && d.rem == 2, 11);
    ldiv_t l = ldiv(100003L, 7L);
    chk(l.quot == 14286 && l.rem == 1, 12);
  }

  /* --- rand / srand: deterministic + in range --- */
  {
    srand(1);
    int r1 = rand();
    srand(1);
    int r2 = rand();
    chk(r1 == r2 && r1 >= 0 && r1 <= RAND_MAX, 13);
  }

  /* --- qsort / bsearch --- */
  {
    int arr[6] = {5, 3, 8, 1, 9, 2};
    qsort(arr, 6, sizeof(int), icmp);
    chk(arr[0] == 1 && arr[5] == 9, 14);
    int key = 8;
    int *p = (int *)bsearch(&key, arr, 6, sizeof(int), icmp);
    chk(p && *p == 8, 15);
    int miss = 7;
    chk(bsearch(&miss, arr, 6, sizeof(int), icmp) == NULL, 16);
  }

  /* --- sscanf --- */
  {
    int x, y, h;
    char s[16];
    int n = sscanf("42 abc 7", "%d %s %d", &x, s, &y);
    chk(n == 3 && x == 42 && y == 7 && strcmp(s, "abc") == 0, 17);
    sscanf("ff", "%x", &h);
    chk(h == 255, 18);
    float f;
    sscanf("3.5", "%f", &f);
    chk(dclose((double)f, 3.5), 19);
  }

  /* --- <string.h> cross-section --- */
  {
    char buf[16];
    strcpy(buf, "hi");
    strcat(buf, " there");
    chk(strcmp(buf, "hi there") == 0, 20);
    chk(strncmp("abcd", "abxx", 2) == 0, 21);
    chk(strchr("hello", 'l') != NULL, 22);
    chk(strstr("hello world", "world") != NULL, 23);
    chk(memcmp("abc", "abc", 3) == 0, 24);
    chk((int)strlen("count") == 5, 25);
  }

  /* --- <signal.h>: install + raise dispatches synchronously --- */
  {
    signal(SIGTERM, onsig);
    raise(SIGTERM);
    chk(sig_hit == 1, 26);
  }

  printf("LIB PASS %d/%d\n", passes, total);
  return (passes == total) ? 0 : 1;
}
