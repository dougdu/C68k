#include <wctype.h>
#include <ctype.h>

/* "C" locale: characters < 0x80 defer to <ctype.h>; everything else (including
 * WEOF) is unclassified, and case mapping is the identity. */
#define _ASCII(c) ((wint_t)(c) < 0x80)

int iswalnum(wint_t c) { return _ASCII(c) && isalnum((int)c); }
int iswalpha(wint_t c) { return _ASCII(c) && isalpha((int)c); }
int iswblank(wint_t c) { return _ASCII(c) && isblank((int)c); }
int iswcntrl(wint_t c) { return _ASCII(c) && iscntrl((int)c); }
int iswdigit(wint_t c) { return _ASCII(c) && isdigit((int)c); }
int iswgraph(wint_t c) { return _ASCII(c) && isgraph((int)c); }
int iswlower(wint_t c) { return _ASCII(c) && islower((int)c); }
int iswprint(wint_t c) { return _ASCII(c) && isprint((int)c); }
int iswpunct(wint_t c) { return _ASCII(c) && ispunct((int)c); }
int iswspace(wint_t c) { return _ASCII(c) && isspace((int)c); }
int iswupper(wint_t c) { return _ASCII(c) && isupper((int)c); }
int iswxdigit(wint_t c) { return _ASCII(c) && isxdigit((int)c); }
wint_t towlower(wint_t c) { return _ASCII(c) ? (wint_t)tolower((int)c) : c; }
wint_t towupper(wint_t c) { return _ASCII(c) ? (wint_t)toupper((int)c) : c; }

enum {
  _WCT_ALNUM = 1,
  _WCT_ALPHA,
  _WCT_BLANK,
  _WCT_CNTRL,
  _WCT_DIGIT,
  _WCT_GRAPH,
  _WCT_LOWER,
  _WCT_PRINT,
  _WCT_PUNCT,
  _WCT_SPACE,
  _WCT_UPPER,
  _WCT_XDIGIT
};

static int _streq(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

wctype_t wctype(const char *name) {
  if (_streq(name, "alnum"))
    return _WCT_ALNUM;
  if (_streq(name, "alpha"))
    return _WCT_ALPHA;
  if (_streq(name, "blank"))
    return _WCT_BLANK;
  if (_streq(name, "cntrl"))
    return _WCT_CNTRL;
  if (_streq(name, "digit"))
    return _WCT_DIGIT;
  if (_streq(name, "graph"))
    return _WCT_GRAPH;
  if (_streq(name, "lower"))
    return _WCT_LOWER;
  if (_streq(name, "print"))
    return _WCT_PRINT;
  if (_streq(name, "punct"))
    return _WCT_PUNCT;
  if (_streq(name, "space"))
    return _WCT_SPACE;
  if (_streq(name, "upper"))
    return _WCT_UPPER;
  if (_streq(name, "xdigit"))
    return _WCT_XDIGIT;
  return 0;
}

int iswctype(wint_t c, wctype_t desc) {
  switch (desc) {
  case _WCT_ALNUM:
    return iswalnum(c);
  case _WCT_ALPHA:
    return iswalpha(c);
  case _WCT_BLANK:
    return iswblank(c);
  case _WCT_CNTRL:
    return iswcntrl(c);
  case _WCT_DIGIT:
    return iswdigit(c);
  case _WCT_GRAPH:
    return iswgraph(c);
  case _WCT_LOWER:
    return iswlower(c);
  case _WCT_PRINT:
    return iswprint(c);
  case _WCT_PUNCT:
    return iswpunct(c);
  case _WCT_SPACE:
    return iswspace(c);
  case _WCT_UPPER:
    return iswupper(c);
  case _WCT_XDIGIT:
    return iswxdigit(c);
  default:
    return 0;
  }
}

wctrans_t wctrans(const char *name) {
  if (_streq(name, "tolower"))
    return 1;
  if (_streq(name, "toupper"))
    return 2;
  return 0;
}

wint_t towctrans(wint_t c, wctrans_t desc) {
  if (desc == 2)
    return towupper(c);
  if (desc == 1)
    return towlower(c);
  return c;
}
