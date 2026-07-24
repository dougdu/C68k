#ifndef _FENV_H
#define _FENV_H

/* Soft-float floating-point environment.  The runtime has fixed
 * round-to-nearest and no exception flags, so this is a conforming-but-inert
 * <fenv.h>: the exception operations are no-ops, and only FE_TONEAREST can be
 * selected.  (math_errhandling in <math.h> is MATH_ERRNO, not MATH_ERREXCEPT.) */

typedef struct {
  unsigned long __ctrl;
} fenv_t;
typedef unsigned long fexcept_t;

/* Exception flags (defined so code compiles; none are ever raised). */
#define FE_DIVBYZERO 0x01
#define FE_INEXACT 0x02
#define FE_INVALID 0x04
#define FE_OVERFLOW 0x08
#define FE_UNDERFLOW 0x10
#define FE_ALL_EXCEPT                                                          \
  (FE_DIVBYZERO | FE_INEXACT | FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW)

/* Rounding directions -- only FE_TONEAREST is available. */
#define FE_TONEAREST 0
#define FE_DOWNWARD 1
#define FE_UPWARD 2
#define FE_TOWARDZERO 3

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
