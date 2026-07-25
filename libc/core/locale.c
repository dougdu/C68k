#include <locale.h>
#include <limits.h>
#include <string.h>
#include <stddef.h>
#include "libc_internal.h"

/* Locale support.  "C"/"POSIX" are always available.  The native locale ("")
 * adopts the OS-configured country on Osiris (DOS 38h country block), so
 * localeconv() reports that country's numeric/monetary conventions; on
 * CP/M-68K (no country service) "" falls back to the "C" locale.  Collation
 * (strcoll/strxfrm, strcoll.c) uses the "C" byte order for every locale. */
static char _c_name[] = "C";
static char _native_name[] = ""; /* returned for the native ("") locale */

/* The C-locale lconv: '.' decimal point, everything else empty/unspecified
 * (CHAR_MAX marks a value that is not available in the "C" locale). */
static struct lconv _c_lconv = {
    ".", "", "", "", "", "", "", "", "", "",
    CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
    CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
};

static int _native;           /* 1 = the native ("") locale is active */
static struct lconv _n_lconv; /* built from the country block */
static char _n_dec[4], _n_thou[4], _n_cur[8]; /* backing strings for _n_lconv */
static unsigned char _coll_tbl[256]; /* native LC_COLLATE weights (OS 65h/06) */

/* Copy an ASCIIZ field of at most `max` bytes out of the country block. */
static void _copyz(char *dst, const unsigned char *src, int max) {
  int i = 0;
  while (i < max && src[i]) {
    dst[i] = (char)src[i];
    i++;
  }
  dst[i] = 0;
}

/* Populate _n_lconv from the OS country block; returns 0 on success. */
static int _load_native(void) {
  long aligned[9]; /* 36 bytes, even-aligned: DOS writes a longword at 12h */
  unsigned char *blk = (unsigned char *)aligned;
  if (sys_getcountry(0, blk) != 0)
    return -1; /* no country service (e.g. CP/M) */
  _copyz(_n_thou, blk + 0x07, 2); /* thousands separator */
  _copyz(_n_dec, blk + 0x09, 2);  /* decimal separator */
  _copyz(_n_cur, blk + 0x02, 4);  /* currency symbol */
  _n_lconv = _c_lconv;            /* inherit the C defaults, then fill in */
  _n_lconv.decimal_point = _n_dec[0] ? _n_dec : ".";
  _n_lconv.thousands_sep = _n_thou;
  _n_lconv.grouping = "\3"; /* DOS has no grouping field: groups of 3 */
  _n_lconv.currency_symbol = _n_cur;
  _n_lconv.mon_decimal_point = _n_lconv.decimal_point;
  _n_lconv.mon_thousands_sep = _n_thou;
  _n_lconv.mon_grouping = "\3";
  _n_lconv.int_frac_digits = (char)blk[0x10]; /* currency significant digits */
  _n_lconv.frac_digits = (char)blk[0x10];
  {
    int fmt = blk[0x0F]; /* 0 pre | 1 post | 2 pre+space | 3 post+space | 4 dp */
    char pre = (fmt == 0 || fmt == 2) ? 1 : 0;
    char sp = (fmt == 2 || fmt == 3) ? 1 : 0;
    _n_lconv.p_cs_precedes = pre;
    _n_lconv.n_cs_precedes = pre;
    _n_lconv.p_sep_by_space = sp;
    _n_lconv.n_sep_by_space = sp;
  }
  /* LC_COLLATE: adopt the OS collating sequence (65h/06) when available so
     strcoll/strxfrm order per the country table; else leave byte order. */
  _coll_weights = 0;
  if (sys_getcolltab(_coll_tbl) == 0)
    _coll_weights = _coll_tbl;
  return 0;
}

char *setlocale(int category, const char *locale) {
  (void)category;
  if (locale == NULL)
    return _native ? _native_name : _c_name; /* query the current locale */
  if (strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0) {
    _native = 0;
    _coll_weights = 0; /* C locale: byte-order collation */
    return _c_name;
  }
  if (locale[0] == '\0') { /* the OS-configured native locale */
    if (_load_native() == 0) {
      _native = 1;
      return _native_name;
    }
    _native = 0; /* none available -> the C locale */
    _coll_weights = 0;
    return _c_name;
  }
  return NULL; /* any named non-C locale is unavailable */
}

struct lconv *localeconv(void) { return _native ? &_n_lconv : &_c_lconv; }
