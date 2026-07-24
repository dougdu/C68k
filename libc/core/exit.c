#include <stdio.h>
#include <stdlib.h>
#include "libc_internal.h"

/* =====================================================================
 * process exit -- run atexit handlers (LIFO), flush all output streams, then
 * hand off to the OS.  exit is NOT force-linked: crt0 calls _sys_exit
 * directly for a normal main() return, so a program that never calls
 * exit()/atexit()/abort() pulls none of this (and no stdio via the flush).
 * ===================================================================== */
#define _ATEXIT_MAX 32
static void (*_atexit_fns[_ATEXIT_MAX])(void);
static int _atexit_n;

int atexit(void (*fn)(void)) {
  if (_atexit_n >= _ATEXIT_MAX)
    return -1;
  _atexit_fns[_atexit_n++] = fn;
  return 0;
}

void exit(int code) {
  while (_atexit_n > 0)
    _atexit_fns[--_atexit_n]();
  fflush(NULL);
  sys_exit(code);
  for (;;)
    ;
}

void abort(void) { exit(1); }

/* C11 quick_exit: run the at_quick_exit handlers (LIFO), then _Exit -- no
 * atexit handlers and no stdio flush. */
#define _QEXIT_MAX 32
static void (*_qexit_fns[_QEXIT_MAX])(void);
static int _qexit_n;

int at_quick_exit(void (*fn)(void)) {
  if (_qexit_n >= _QEXIT_MAX)
    return -1;
  _qexit_fns[_qexit_n++] = fn;
  return 0;
}

void quick_exit(int code) {
  while (_qexit_n > 0)
    _qexit_fns[--_qexit_n]();
  _Exit(code);
}
