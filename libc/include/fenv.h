#ifndef _FENV_H
#define _FENV_H

/* Floating-point environment, backed by the libm IEEE-754 runtime
 * (core/fenv.a68).  The core arithmetic ops raise sticky exception flags and
 * honour all four rounding directions, so <fenv.h> is functional here.  The
 * flag and rounding-mode values below MUST match the library's ieee754.inc --
 * the ops set those exact bits in the shared status word.  math_errhandling in
 * <math.h> stays MATH_ERRNO: the math functions report domain/range errors via
 * errno, not via these flags. */

typedef struct {
  unsigned long __stat;  /* sticky exception flags */
  unsigned long __round; /* rounding-direction mode */
} fenv_t;
typedef unsigned long fexcept_t;

/* Exception flags -- values match libm ieee754.inc (IEEE 754 clause-7 order). */
#define FE_INVALID 0x01
#define FE_DIVBYZERO 0x02
#define FE_OVERFLOW 0x04
#define FE_UNDERFLOW 0x08
#define FE_INEXACT 0x10
#define FE_ALL_EXCEPT                                                          \
  (FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT)

/* Rounding directions -- values match libm ieee754.inc. */
#define FE_TONEAREST 0
#define FE_TOWARDZERO 1
#define FE_UPWARD 2
#define FE_DOWNWARD 3

#define FE_DFL_ENV ((const fenv_t *)-1)

int feclearexcept(int excepts);
int fegetexceptflag(fexcept_t *flagp, int excepts);
int feraiseexcept(int excepts);
int fesetexceptflag(const fexcept_t *flagp, int excepts);
int fetestexcept(int excepts);
int fegetround(void);
int fesetround(int round);
int fegetenv(fenv_t *envp);
int feholdexcept(fenv_t *envp);
int fesetenv(const fenv_t *envp);
int feupdateenv(const fenv_t *envp);

#endif /* _FENV_H */
