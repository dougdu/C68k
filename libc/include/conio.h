#ifndef _CONIO_H
#define _CONIO_H

/* Turbo/Watcom-style direct console I/O.  getch/getche/kbhit use the raw
 * (unbuffered, no line-editing) console; clrscr/gotoxy emit ANSI/VT100 escapes
 * and therefore depend on the console driver (Osiris ANSI.SYS supports them). */
int getch(void);
int getche(void);
int putch(int c);
int kbhit(void);
int cputs(const char *s);
void clrscr(void);
void gotoxy(int x, int y);

#endif /* _CONIO_H */
