#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>

int unlink(const char *path);
int close(int fd);

#endif /* _UNISTD_H */
