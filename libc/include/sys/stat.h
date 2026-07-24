#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>
#include <time.h>

/* File type + permission bits for st_mode.  These targets model only regular
 * files, directories, and character devices (the console); the permission bits
 * are an approximation from the DOS read-only attribute. */
#define S_IFMT 0xF000
#define S_IFREG 0x8000
#define S_IFDIR 0x4000
#define S_IFCHR 0x2000

#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)

#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 0070
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXO 0007
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001

struct stat {
  long st_dev;
  long st_ino;
  long st_mode;
  long st_nlink;
  long st_uid;
  long st_gid;
  long st_size;
  time_t st_atime;
  time_t st_mtime;
  time_t st_ctime;
};

int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int lstat(const char *path, struct stat *st);
int mkdir(const char *path, mode_t mode);
int chmod(const char *path, mode_t mode);

#endif /* _SYS_STAT_H */
