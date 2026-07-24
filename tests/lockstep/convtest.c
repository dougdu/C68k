/* P7 ubiquitous conveniences -- getopt(_long), strlcpy/strlcat/strsep/
 * strcasestr/memmem, reallocarray, qsort_r, itoa family, progname.  Cross-OS
 * lockstep test.  Prints "CONVTEST PASS n/n". */
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <stdio.h>

static int pass, total;
#define CHECK(c)                                                               \
  do {                                                                         \
    total++;                                                                   \
    if (c)                                                                     \
      pass++;                                                                  \
    else                                                                       \
      printf("FAIL line %d\n", __LINE__);                                      \
  } while (0)

static int cmp_r(const void *a, const void *b, void *arg) {
  int sign = *(int *)arg;
  return sign * (*(const int *)a - *(const int *)b);
}

int main(void) {
  /* getopt: -a -bvalue -c arg positional */
  char *av[] = {"prog", "-a", "-bvalue", "-c", "arg", "positional", 0};
  optind = 1;
  int a = 0, b = 0, c = 0, o;
  char *bv = 0;
  while ((o = getopt(6, av, "ab:c")) != -1) {
    if (o == 'a')
      a = 1;
    else if (o == 'b') {
      b = 1;
      bv = optarg;
    } else if (o == 'c')
      c = 1;
  }
  CHECK(a && b && c && bv && strcmp(bv, "value") == 0);
  CHECK(strcmp(av[optind], "arg") == 0);

  /* getopt_long */
  struct option lo[] = {{"verbose", no_argument, 0, 'v'},
                        {"file", required_argument, 0, 'f'},
                        {0, 0, 0, 0}};
  char *lv[] = {"prog", "--verbose", "--file=x.txt", "rest", 0};
  optind = 1;
  int vseen = 0, li = -1;
  char *fv = 0;
  while ((o = getopt_long(4, lv, "vf:", lo, &li)) != -1) {
    if (o == 'v')
      vseen = 1;
    else if (o == 'f')
      fv = optarg;
  }
  CHECK(vseen && fv && strcmp(fv, "x.txt") == 0);
  CHECK(strcmp(lv[optind], "rest") == 0);

  /* strlcpy / strlcat */
  char buf[8];
  CHECK(strlcpy(buf, "hello", sizeof buf) == 5 && strcmp(buf, "hello") == 0);
  CHECK(strlcpy(buf, "toolongstring", sizeof buf) == 13 &&
        strcmp(buf, "toolong") == 0);
  strcpy(buf, "ab");
  CHECK(strlcat(buf, "cdef", sizeof buf) == 6 && strcmp(buf, "abcdef") == 0);

  /* strsep (with an empty field) */
  char str[] = "a,b,,c";
  char *sp = str;
  CHECK(strcmp(strsep(&sp, ","), "a") == 0);
  CHECK(strcmp(strsep(&sp, ","), "b") == 0);
  CHECK(strcmp(strsep(&sp, ","), "") == 0);
  CHECK(strcmp(strsep(&sp, ","), "c") == 0);
  CHECK(strsep(&sp, ",") == 0);

  /* strcasestr / memmem */
  CHECK(strcasestr("Hello World", "WORLD") != 0);
  CHECK(strcasestr("abc", "xyz") == 0);
  CHECK(memmem("abcdef", 6, "cd", 2) != 0);
  CHECK(memmem("abcdef", 6, "xy", 2) == 0);
  CHECK(memmem("abc", 3, "", 0) != 0);

  /* reallocarray */
  void *p = reallocarray(0, 4, 10);
  CHECK(p != 0);
  free(p);
  errno = 0;
  CHECK(reallocarray(0, (size_t)-1, 2) == 0 && errno == ENOMEM);

  /* qsort_r (descending via the context argument) */
  int arr[] = {3, 1, 2}, desc = -1;
  qsort_r(arr, 3, sizeof(int), cmp_r, &desc);
  CHECK(arr[0] == 3 && arr[1] == 2 && arr[2] == 1);

  /* itoa family */
  char nb[34];
  CHECK(strcmp(itoa(-42, nb, 10), "-42") == 0);
  CHECK(strcmp(itoa(255, nb, 16), "ff") == 0);
  CHECK(strcmp(utoa(255u, nb, 2), "11111111") == 0);
  CHECK(strcmp(ltoa(-1000000L, nb, 10), "-1000000") == 0);
  CHECK(strcmp(ultoa(4000000000UL, nb, 10), "4000000000") == 0);

  /* progname (used by <err.h>) */
  setprogname("myprog");
  CHECK(strcmp(getprogname(), "myprog") == 0);

  printf("CONVTEST PASS %d/%d\n", pass, total);
  return 0;
}
