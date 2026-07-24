/* Osiris-only: directory ops (mkdir/rmdir/chdir/getcwd) + utime over DOS
 * 39h/3Ah/3Bh/47h/57h.  CP/M-68K has no subdirectories, so this is not a
 * lockstep case; run it through tools/osiris/run-osiris.ps1.
 * Prints "MKDIRP PASS n/n". */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>

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
  struct stat st;
  char cwd[80];

  CHECK(mkdir("SUBD", 0755) == 0);
  CHECK(stat("SUBD", &st) == 0 && S_ISDIR(st.st_mode));

  CHECK(chdir("SUBD") == 0);
  CHECK(getcwd(cwd, sizeof cwd) != NULL && strstr(cwd, "SUBD") != NULL);

  int fd = open("INNER.TMP", O_CREAT | O_WRONLY | O_TRUNC, 0);
  CHECK(fd >= 0);
  CHECK(write(fd, "inner", 5) == 5);
  close(fd);
  CHECK(stat("INNER.TMP", &st) == 0 && st.st_size == 5);

  /* utime sets the modification time; stat reads it back (2-second DOS
     resolution, so allow a small delta). */
  struct utimbuf ut;
  ut.actime = ut.modtime = 1000000000L; /* 2001-09-09 */
  CHECK(utime("INNER.TMP", &ut) == 0);
  CHECK(stat("INNER.TMP", &st) == 0);
  {
    long d = st.st_mtime - 1000000000L;
    if (d < 0)
      d = -d;
    CHECK(d <= 2);
  }

  unlink("INNER.TMP");
  CHECK(chdir("..") == 0);
  CHECK(rmdir("SUBD") == 0);

  printf("MKDIRP PASS %d/%d\n", pass, total);
  return 0;
}
