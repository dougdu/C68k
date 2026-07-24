#ifndef _WCTYPE_H
#define _WCTYPE_H

#ifndef __WINT_T_DEFINED
#define __WINT_T_DEFINED
typedef unsigned int wint_t;
#endif

#ifndef WEOF
#define WEOF ((wint_t)-1)
#endif

typedef int wctype_t;
typedef int wctrans_t;

/* Wide-character classification. The execution locale is "C": characters below
 * 0x80 classify via <ctype.h>; everything else is unclassified. */
int iswalnum(wint_t c);
int iswalpha(wint_t c);
int iswblank(wint_t c);
int iswcntrl(wint_t c);
int iswdigit(wint_t c);
int iswgraph(wint_t c);
int iswlower(wint_t c);
int iswprint(wint_t c);
int iswpunct(wint_t c);
int iswspace(wint_t c);
int iswupper(wint_t c);
int iswxdigit(wint_t c);
wint_t towlower(wint_t c);
wint_t towupper(wint_t c);
wctype_t wctype(const char *name);
int iswctype(wint_t c, wctype_t desc);
wctrans_t wctrans(const char *name);
wint_t towctrans(wint_t c, wctrans_t desc);

#endif /* _WCTYPE_H */
