#ifndef __STDARG_H
#define __STDARG_H

/*
 * m68k va_arg: every argument is passed on the stack, each occupying a
 * 4-byte-aligned slot (8 bytes for double / long long). The code generator
 * stores a pointer to the first variadic argument into __va_area__ in the
 * function prologue; va_arg walks that pointer forward.
 */

typedef char *va_list[1];

#define va_start(ap, last) ((void)(last), (ap)[0] = *(char **)__va_area__)
#define va_end(ap) ((void)0)
#define va_copy(dst, src) ((dst)[0] = (src)[0])

#define __va_round(t) ((sizeof(t) + 3) & ~3)
#define va_arg(ap, type)                                                        \
  (*(type *)(((ap)[0] += __va_round(type)), (ap)[0] - __va_round(type)))

#endif /* __STDARG_H */
