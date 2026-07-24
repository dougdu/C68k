/* P1 POSIX file-descriptor layer -- self-checking lockstep smoke test.
 * Prints "FDTEST PASS n/n" when every check holds.  Runs identically on
 * Osiris and CP/M-68K.  (dup/dup2 are Osiris-only -- see dupprobe.c.) */
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
  char buf[16];
  int fd, n;

  /* open(O_CREAT|O_WRONLY|O_TRUNC) + write + close */
  fd = open("FDTEST.TMP", O_CREAT | O_WRONLY | O_TRUNC, 0);
  CHECK(fd >= 0);
  CHECK(write(fd, "hello", 5) == 5);
  CHECK(close(fd) == 0);

  /* open(O_RDONLY) + read back */
  fd = open("FDTEST.TMP", O_RDONLY);
  CHECK(fd >= 0);
  n = read(fd, buf, 5);
  CHECK(n == 5 && memcmp(buf, "hello", 5) == 0);
  CHECK(close(fd) == 0);

  /* lseek within the file (byte 2 lands in the first CP/M record) */
  fd = open("FDTEST.TMP", O_RDONLY);
  CHECK(lseek(fd, 2, SEEK_SET) == 2);
  n = read(fd, buf, 3);
  CHECK(n == 3 && memcmp(buf, "llo", 3) == 0);
  close(fd);

  /* access(): existing vs missing */
  CHECK(access("FDTEST.TMP", F_OK) == 0);
  CHECK(access("NOSUCH.TMP", F_OK) == -1);

  /* fileno / isatty: stdout is the console, a file is not */
  CHECK(fileno(stdout) == STDOUT_FILENO);
  CHECK(isatty(fileno(stdout)) == 1);
  fd = open("FDTEST.TMP", O_RDONLY);
  CHECK(isatty(fd) == 0);
  close(fd);

  /* creat() + fdopen(): wrap a raw descriptor in a buffered stream */
  fd = creat("FDT2.TMP", 0);
  CHECK(fd >= 0);
  CHECK(write(fd, "AB", 2) == 2);
  close(fd);
  fd = open("FDT2.TMP", O_RDONLY);
  FILE *fp = fdopen(fd, "rb");
  CHECK(fp != NULL);
  CHECK(fgetc(fp) == 'A');
  CHECK(fgetc(fp) == 'B');
  fclose(fp);

  unlink("FDTEST.TMP");
  unlink("FDT2.TMP");

  printf("FDTEST PASS %d/%d\n", pass, total);
  return 0;
}
