#ifndef _CPM_H
#define _CPM_H

/* Direct CP/M-68K BDOS (TRAP #2) access -- an escape hatch for BDOS services
 * the portable library does not wrap.  CP/M-only: on Osiris there is no BDOS,
 * so bdos() returns -1. */
long bdos(int func, long param);

#endif /* _CPM_H */
