#include <uchar.h>
#include <errno.h>

/* UTF-8 <-> UTF-16/UTF-32 conversion (the C locale is UTF-8 here). */

static int u8len(unsigned char b) {
  if (b < 0x80)
    return 1;
  if ((b & 0xE0) == 0xC0)
    return 2;
  if ((b & 0xF0) == 0xE0)
    return 3;
  if ((b & 0xF8) == 0xF0)
    return 4;
  return -1;
}

/* Combine the ps->__explen bytes in ps->__buf into *cp, rejecting overlong
 * encodings, surrogates, and values above U+10FFFF. */
static int combine(mbstate_t *ps, unsigned long *cp) {
  unsigned char *b = ps->__buf;
  int len = ps->__explen;
  unsigned long v;
  static const unsigned long minv[5] = {0, 0, 0x80, 0x800, 0x10000};
  if (len == 1)
    v = b[0];
  else if (len == 2)
    v = ((unsigned long)(b[0] & 0x1F) << 6) | (b[1] & 0x3F);
  else if (len == 3)
    v = ((unsigned long)(b[0] & 0x0F) << 12) |
        ((unsigned long)(b[1] & 0x3F) << 6) | (b[2] & 0x3F);
  else
    v = ((unsigned long)(b[0] & 0x07) << 18) |
        ((unsigned long)(b[1] & 0x3F) << 12) |
        ((unsigned long)(b[2] & 0x3F) << 6) | (b[3] & 0x3F);
  if (v < minv[len] || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
    return -1;
  *cp = v;
  return 0;
}

/* Accumulate bytes from s (up to n) into ps until a full sequence is present,
 * then decode into *cp.  Returns bytes consumed, (size_t)-2 if incomplete,
 * or (size_t)-1 (EILSEQ) on a malformed sequence. */
static size_t decode_stream(unsigned long *cp, const char *s, size_t n,
                            mbstate_t *ps) {
  size_t used = 0;
  if (ps->__nbytes == 0) {
    if (n == 0)
      return (size_t)-2;
    unsigned char b0 = (unsigned char)s[used];
    int len = u8len(b0);
    if (len < 0) {
      errno = EILSEQ;
      return (size_t)-1;
    }
    ps->__buf[0] = b0;
    ps->__nbytes = 1;
    ps->__explen = (unsigned char)len;
    used++;
    n--;
  }
  while (ps->__nbytes < ps->__explen) {
    if (n == 0)
      return (size_t)-2;
    unsigned char b = (unsigned char)s[used];
    if ((b & 0xC0) != 0x80) {
      errno = EILSEQ;
      ps->__nbytes = 0;
      return (size_t)-1;
    }
    ps->__buf[ps->__nbytes++] = b;
    used++;
    n--;
  }
  if (combine(ps, cp) != 0) {
    errno = EILSEQ;
    ps->__nbytes = 0;
    return (size_t)-1;
  }
  ps->__nbytes = 0;
  return used;
}

static size_t encode_utf8(char *s, unsigned long cp) {
  if (cp < 0x80) {
    s[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    s[0] = (char)(0xC0 | (cp >> 6));
    s[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    s[0] = (char)(0xE0 | (cp >> 12));
    s[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    s[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  s[0] = (char)(0xF0 | (cp >> 18));
  s[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  s[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  s[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

static mbstate_t _mbrtoc16_st, _mbrtoc32_st, _c16rtomb_st;

size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps) {
  if (!ps)
    ps = &_mbrtoc32_st;
  if (!s) {
    ps->__nbytes = 0;
    ps->__pending = 0;
    return 0;
  }
  unsigned long cp;
  size_t r = decode_stream(&cp, s, n, ps);
  if (r == (size_t)-1 || r == (size_t)-2)
    return r;
  if (pc32)
    *pc32 = (char32_t)cp;
  return cp == 0 ? 0 : r;
}

size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps) {
  if (!ps)
    ps = &_mbrtoc16_st;
  if (ps->__pending) {
    if (pc16)
      *pc16 = (char16_t)ps->__pending;
    ps->__pending = 0;
    return (size_t)-3;
  }
  if (!s) {
    ps->__nbytes = 0;
    ps->__pending = 0;
    return 0;
  }
  unsigned long cp;
  size_t r = decode_stream(&cp, s, n, ps);
  if (r == (size_t)-1 || r == (size_t)-2)
    return r;
  if (cp > 0xFFFF) {
    cp -= 0x10000;
    if (pc16)
      *pc16 = (char16_t)(0xD800 + (cp >> 10));
    ps->__pending = (unsigned short)(0xDC00 + (cp & 0x3FF));
    return r;
  }
  if (pc16)
    *pc16 = (char16_t)cp;
  return cp == 0 ? 0 : r;
}

size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps) {
  (void)ps;
  if (!s)
    return 1;
  if (c32 > 0x10FFFF || (c32 >= 0xD800 && c32 <= 0xDFFF)) {
    errno = EILSEQ;
    return (size_t)-1;
  }
  return encode_utf8(s, c32);
}

size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps) {
  if (!ps)
    ps = &_c16rtomb_st;
  if (!s) {
    ps->__pending = 0;
    return 1;
  }
  if (c16 >= 0xD800 && c16 <= 0xDBFF) { /* high surrogate: hold it */
    ps->__pending = c16;
    return 0;
  }
  if (c16 >= 0xDC00 && c16 <= 0xDFFF) { /* low surrogate: combine */
    if (ps->__pending < 0xD800 || ps->__pending > 0xDBFF) {
      errno = EILSEQ;
      return (size_t)-1;
    }
    unsigned long cp =
        0x10000 + (((unsigned long)(ps->__pending - 0xD800) << 10) |
                   (unsigned long)(c16 - 0xDC00));
    ps->__pending = 0;
    return encode_utf8(s, cp);
  }
  if (ps->__pending) { /* a high surrogate not followed by a low one */
    errno = EILSEQ;
    return (size_t)-1;
  }
  return encode_utf8(s, c16);
}
