/* Osiris-only: dup()/dup2() over DOS 45h/46h.  Prints "DUPPROBE PASS n/n".
 * CP/M-68K has no descriptor duplication, so this is not a lockstep case;
 * run it through tools/osiris/run-osiris.ps1. */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static int pass, total;
#define CHECK(c)                                                               \
  do {                                                                         \
    total++;                                                                   \
    if (c)                                                                     \
      pass++;                                                                  \
    else                                                                       \
      printf("FAIL line %d\n", __LINE__);                                      \
  } while (0)

int main(void) {
  char b[4];

  /* dup(stdout): a valid, distinct handle that still writes to the console. */
  int d = dup(STDOUT_FILENO);
  CHECK(d >= 0 && d != STDOUT_FILENO);
  CHECK(write(d, "DUP_WROTE\n", 10) == 10);
  close(d);

  /* A dup shares the file offset with the original (same open-file entry). */
  int fd = open("DUPT.TMP", O_CREAT | O_WRONLY | O_TRUNC, 0);
  CHECK(fd >= 0);
  int e = dup(fd);
  CHECK(e >= 0 && e != fd);
  CHECK(write(e, "XY", 2) == 2);  /* advances the shared position to 2 */
  CHECK(write(fd, "Z", 1) == 1);  /* continues at 2, not 0 */
  close(e);
  close(fd);
  fd = open("DUPT.TMP", O_RDONLY);
  CHECK(read(fd, b, 3) == 3 && memcmp(b, "XYZ", 3) == 0);
  close(fd);

  /* dup2 onto an explicit free target handle returns that handle. */
  fd = open("DUPT.TMP", O_RDONLY);
  int t = dup2(fd, 6);
  CHECK(t == 6);
  CHECK(read(t, b, 3) == 3 && memcmp(b, "XYZ", 3) == 0);
  close(t);
  unlink("DUPT.TMP");

  printf("DUPPROBE PASS %d/%d\n", pass, total);
  return 0;
}
