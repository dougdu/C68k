/* P2/P3: stat/fstat + <dirent.h> + chmod/getcwd -- cross-OS lockstep test.
 * Prints "STATDIR PASS n/n".  Uses a 128-byte file so the size is identical on
 * Osiris (byte-exact) and CP/M-68K (record-granular, 1 record = 128 bytes). */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
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
  char blk[128];
  memset(blk, 'x', sizeof blk);

  int fd = open("STATF.TMP", O_CREAT | O_WRONLY | O_TRUNC, 0);
  CHECK(fd >= 0);
  CHECK(write(fd, blk, 128) == 128);
  close(fd);

  struct stat st;
  CHECK(stat("STATF.TMP", &st) == 0);
  CHECK(S_ISREG(st.st_mode));
  CHECK(!S_ISDIR(st.st_mode));
  CHECK(st.st_size == 128);

  fd = open("STATF.TMP", O_RDONLY);
  CHECK(fstat(fd, &st) == 0);
  CHECK(S_ISREG(st.st_mode));
  CHECK(st.st_size == 128);
  close(fd);

  CHECK(lstat("STATF.TMP", &st) == 0 && st.st_size == 128);

  /* Exact (not record-granular) size: a 10-byte file reports 10 on both OSes --
     Osiris stores the exact length; CP/M trims trailing ^Z padding. */
  fd = open("SZ10.TMP", O_CREAT | O_WRONLY | O_TRUNC, 0);
  write(fd, "0123456789", 10);
  close(fd);
  CHECK(stat("SZ10.TMP", &st) == 0 && st.st_size == 10);
  fd = open("SZ10.TMP", O_RDONLY);
  CHECK(fstat(fd, &st) == 0 && st.st_size == 10);
  close(fd);
  unlink("SZ10.TMP");

  /* opendir + readdir must surface the file we just created. */
  DIR *d = opendir(".");
  CHECK(d != NULL);
  int found = 0;
  struct dirent *de;
  while ((de = readdir(d)) != NULL)
    if (strcasecmp(de->d_name, "STATF.TMP") == 0)
      found = 1;
  CHECK(found);
  closedir(d);

  /* chmod read-only, then restore. */
  CHECK(chmod("STATF.TMP", 0444) == 0);
  CHECK(stat("STATF.TMP", &st) == 0 && !(st.st_mode & S_IWUSR));
  CHECK(chmod("STATF.TMP", 0644) == 0);
  CHECK(stat("STATF.TMP", &st) == 0 && (st.st_mode & S_IWUSR));

  char cwd[80];
  CHECK(getcwd(cwd, sizeof cwd) != NULL && cwd[1] == ':');

  CHECK(stat("NOSUCH.TMP", &st) == -1);

  unlink("STATF.TMP");

  printf("STATDIR PASS %d/%d\n", pass, total);
  return 0;
}
