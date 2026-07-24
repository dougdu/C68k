#include <sys/stat.h>
#include <errno.h>
#include "libc_internal.h"

extern long __days_from_civil(int y, int m, int d);

/* FAT/DOS packed date+time -> Unix time_t (UTC).
 *   date: bits 15-9 = year-1980, 8-5 = month, 4-0 = day
 *   time: bits 15-11 = hour, 10-5 = minute, 4-0 = seconds/2 */
static time_t dosdt_to_time(unsigned date, unsigned time) {
  if (!date && !time)
    return 0;
  int y = 1980 + ((date >> 9) & 0x7F);
  int mo = (date >> 5) & 0x0F;
  int dy = date & 0x1F;
  int hh = (time >> 11) & 0x1F;
  int mi = (time >> 5) & 0x3F;
  int ss = (time & 0x1F) * 2;
  if (mo < 1)
    mo = 1;
  if (dy < 1)
    dy = 1;
  return (time_t)(__days_from_civil(y, mo, dy) * 86400L + hh * 3600L +
                  mi * 60L + ss);
}

static void fill_common(struct stat *st) {
  st->st_dev = 0;
  st->st_ino = 0;
  st->st_nlink = 1;
  st->st_uid = 0;
  st->st_gid = 0;
}

int stat(const char *path, struct stat *st) {
  long fb[16]; /* even-aligned: the DOS 4Eh find block has word/long fields */
  unsigned char *b = (unsigned char *)fb;
  if (sys_findfirst(path, 0x16, b) != 0) {
    errno = ENOENT;
    return -1;
  }
  unsigned attr = b[21];
  unsigned tm = (b[22] << 8) | b[23];
  unsigned dt = (b[24] << 8) | b[25];
  fill_common(st);
  if (attr & 0x10) {
    st->st_mode = S_IFDIR | 0755;
    st->st_size = 0; /* directories carry no byte length */
  } else {
    st->st_mode = S_IFREG | 0644;
    /* Take the exact size from an opened handle (byte-exact on Osiris,
     * ^Z-trimmed on CP/M) so stat and fstat agree; fall back to the
     * find-block size if the open fails. */
    int fd = sys_open(path, 0);
    if (fd >= 0) {
      long sz = sys_fdsize(fd);
      sys_close(fd);
      st->st_size = (sz < 0) ? 0 : sz;
    } else {
      st->st_size = (long)(((unsigned long)b[26] << 24) |
                           ((unsigned long)b[27] << 16) |
                           ((unsigned long)b[28] << 8) | b[29]);
    }
  }
  if (attr & 0x01)
    st->st_mode &= ~0222; /* read-only */
  st->st_mtime = st->st_atime = st->st_ctime = dosdt_to_time(dt, tm);
  return 0;
}

int fstat(int fd, struct stat *st) {
  long pos = sys_seek(fd, 0, 1); /* SEEK_CUR: save the position */
  long end = sys_fdsize(fd);     /* size (byte-exact on Osiris; ^Z-trimmed on CP/M) */
  if (pos >= 0)
    sys_seek(fd, pos, 0); /* SEEK_SET: restore */
  fill_common(st);
  st->st_size = (end < 0) ? 0 : end;
  st->st_mode = sys_isatty(fd) ? (S_IFCHR | 0666) : (S_IFREG | 0644);
  int r = sys_getfiletime(fd);
  if (r == -1)
    st->st_mtime = st->st_atime = st->st_ctime = 0;
  else
    st->st_mtime = st->st_atime = st->st_ctime =
        dosdt_to_time(((unsigned)r >> 16) & 0xFFFF, (unsigned)r & 0xFFFF);
  return 0;
}

int lstat(const char *path, struct stat *st) { return stat(path, st); }
