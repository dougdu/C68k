#include <fenv.h>

/* No floating-point exception flags and a fixed round-to-nearest mode, so the
 * exception operations are inert and only FE_TONEAREST can be selected. */

int feclearexcept(int excepts) {
  (void)excepts;
  return 0;
}
int fegetexceptflag(fexcept_t *flagp, int excepts) {
  (void)excepts;
  if (flagp)
    *flagp = 0;
  return 0;
}
int feraiseexcept(int excepts) {
  (void)excepts;
  return 0;
}
int fesetexceptflag(const fexcept_t *flagp, int excepts) {
  (void)flagp;
  (void)excepts;
  return 0;
}
int fetestexcept(int excepts) {
  (void)excepts;
  return 0;
}
int fegetround(void) { return FE_TONEAREST; }
int fesetround(int round) { return (round == FE_TONEAREST) ? 0 : -1; }
int fegetenv(fenv_t *envp) {
  if (envp)
    envp->__ctrl = 0;
  return 0;
}
int feholdexcept(fenv_t *envp) {
  if (envp)
    envp->__ctrl = 0;
  return 0;
}
int fesetenv(const fenv_t *envp) {
  (void)envp;
  return 0;
}
int feupdateenv(const fenv_t *envp) {
  (void)envp;
  return 0;
}
