#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include "libc_internal.h"
#include <signal.h>

/* =====================================================================
 * <signal.h> -- minimal, synchronous. These OSes deliver no async
 * signals, so signal() just records a disposition and raise() dispatches
 * it directly (SIGABRT default terminates via abort()).
 * ===================================================================== */
#define _NSIG 32
static void (*_sigtab[_NSIG])(int);

void (*signal(int sig, void (*func)(int)))(int) {
  if (sig < 1 || sig >= _NSIG)
    return (void (*)(int)) - 1; /* SIG_ERR */
  void (*old)(int) = _sigtab[sig];
  _sigtab[sig] = func;
  return old;
}

int raise(int sig) {
  if (sig < 1 || sig >= _NSIG)
    return -1;
  void (*h)(int) = _sigtab[sig];
  if (h == (void (*)(int))1) /* SIG_IGN */
    return 0;
  if (h != (void (*)(int))0) { /* installed handler */
    h(sig);
    return 0;
  }
  /* SIG_DFL */
  if (sig == 6) /* SIGABRT */
    abort();
  return 0;
}


