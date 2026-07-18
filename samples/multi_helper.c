/* helper.c -- second translation unit for the native multi-TU link demo.
   Provides say() (used by main), which itself calls the crt0/seam. */
extern int sys_write(int fd, const void *buf, int n);

int slen(const char *s) {
  int n = 0;
  while (s[n])
    n++;
  return n;
}

void say(const char *s) { sys_write(1, s, slen(s)); }
