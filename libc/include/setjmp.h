#ifndef _SETJMP_H
#define _SETJMP_H

/*
 * <setjmp.h> -- non-local jumps.
 *
 * jmp_buf holds the caller's resumable context: the return PC, the stack
 * pointer at the point setjmp returns, and the callee-saved register set
 * (D2-D7 / A2-A6).  D0/D1/A0/A1 are caller-saved scratch and are NOT saved --
 * after a longjmp the caller reloads whatever it needs, exactly as it would
 * after any ordinary call.  13 longwords are used; 16 are reserved for
 * headroom.  Implemented in the runtime asm (lib/runtime/rt68k.a68).
 *
 * Standard caveat: an automatic (local) object modified between the setjmp
 * and a longjmp and then read afterwards must be declared `volatile`; its
 * register copy is indeterminate after a longjmp.
 */
typedef unsigned long jmp_buf[16];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
