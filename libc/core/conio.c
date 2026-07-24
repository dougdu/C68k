#include <conio.h>
#include <stdio.h>
#include "libc_internal.h"

int putch(int c) {
  unsigned char b = (unsigned char)c;
  return (sys_write(1, &b, 1) == 1) ? c : EOF;
}

int getch(void) { return sys_conin(); }

int getche(void) {
  int c = sys_conin();
  putch(c);
  return c;
}

int kbhit(void) { return sys_constat(); }

int cputs(const char *s) {
  int n = 0;
  while (*s) {
    if (putch((unsigned char)*s++) == EOF)
      return EOF;
    n++;
  }
  return n;
}

/* Append v (>= 0) as decimal digits at p; returns the new end. */
static char *dec(char *p, int v) {
  if (v < 0)
    v = 0;
  char tmp[12];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  while (n)
    *p++ = tmp[--n];
  return p;
}

void clrscr(void) {
  static const char esc[] = "\033[2J\033[H";
  sys_write(1, esc, (int)(sizeof esc - 1));
}

void gotoxy(int x, int y) {
  char buf[24];
  char *p = buf;
  *p++ = '\033';
  *p++ = '[';
  p = dec(p, y);
  *p++ = ';';
  p = dec(p, x);
  *p++ = 'H';
  sys_write(1, buf, (int)(p - buf));
}
