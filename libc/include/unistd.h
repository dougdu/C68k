#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <sys/types.h> /* ssize_t, off_t */

extern char **environ;

/* access() mode bits. */
#define F_OK 0 /* existence */
#define X_OK 1 /* execute (no execute attribute on these targets) */
#define W_OK 2 /* write */
#define R_OK 4 /* read */

/* Standard stream descriptors. */
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int unlink(const char *path);
int close(int fd);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
off_t lseek(int fd, off_t off, int whence);
int dup(int fd);
int dup2(int oldfd, int newfd);
int isatty(int fd);
int access(const char *path, int mode);
int rmdir(const char *path);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);

/* getopt (POSIX): getopt_long + struct option live in <getopt.h>. */
extern char *optarg;
extern int optind, opterr, optopt;
int getopt(int argc, char *const argv[], const char *optstring);

#endif /* _UNISTD_H */
