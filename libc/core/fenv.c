#include <fenv.h>

/* Real floating-point environment, backed by the libm IEEE-754 _fe_* ABI: the
 * core arithmetic ops raise sticky flags (INVALID/DIVBYZERO/OVERFLOW/UNDERFLOW/
 * INEXACT) and honour all four rounding directions.  c68k prefixes C symbols
 * with '_', so the C names below bind to the library's _fe_* entry points. */
extern int fe_testexcept(int);   /* -> raised subset of the mask         */
extern void fe_clearexcept(int); /* status &= ~mask                       */
extern void fe_raiseexcept(int); /* status |= mask                        */
extern int fe_getround(void);    /* -> current rounding mode              */
extern int fe_setround(int);     /* 0 = accepted, -1 = unsupported mode   */

int feclearexcept(int excepts) {
  fe_clearexcept(excepts & FE_ALL_EXCEPT);
  return 0;
}
int fetestexcept(int excepts) { return fe_testexcept(excepts & FE_ALL_EXCEPT); }
int feraiseexcept(int excepts) {
  fe_raiseexcept(excepts & FE_ALL_EXCEPT);
  return 0;
}
int fegetexceptflag(fexcept_t *flagp, int excepts) {
  if (flagp)
    *flagp = (fexcept_t)fe_testexcept(excepts & FE_ALL_EXCEPT);
  return 0;
}
int fesetexceptflag(const fexcept_t *flagp, int excepts) {
  excepts &= FE_ALL_EXCEPT;
  fe_clearexcept(excepts);
  if (flagp)
    fe_raiseexcept((int)*flagp & excepts);
  return 0;
}
int fegetround(void) { return fe_getround(); }
int fesetround(int round) { return fe_setround(round); }

int fegetenv(fenv_t *envp) {
  if (envp) {
    envp->__stat = (unsigned long)fe_testexcept(FE_ALL_EXCEPT);
    envp->__round = (unsigned long)fe_getround();
  }
  return 0;
}
int feholdexcept(fenv_t *envp) {
  if (envp) {
    envp->__stat = (unsigned long)fe_testexcept(FE_ALL_EXCEPT);
    envp->__round = (unsigned long)fe_getround();
  }
  fe_clearexcept(FE_ALL_EXCEPT);
  return 0;
}
int fesetenv(const fenv_t *envp) {
  fe_clearexcept(FE_ALL_EXCEPT);
  if (envp == FE_DFL_ENV) {
    fe_setround(FE_TONEAREST);
  } else if (envp) {
    fe_raiseexcept((int)envp->__stat & FE_ALL_EXCEPT);
    fe_setround((int)envp->__round);
  }
  return 0;
}
int feupdateenv(const fenv_t *envp) {
  int cur = fe_testexcept(FE_ALL_EXCEPT);
  fesetenv(envp);
  fe_raiseexcept(cur);
  return 0;
}
