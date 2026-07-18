#ifndef _STDDEF_H
#define _STDDEF_H

typedef unsigned long size_t;   /* ILP32: 4 bytes */
typedef long ptrdiff_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t) & (((type *)0)->member))

#endif /* _STDDEF_H */
