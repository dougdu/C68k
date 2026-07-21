#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include "libc_internal.h"
#include <time.h>

/* =====================================================================
 * <time.h> -- wall clock over the seam. The per-OS backend fills a
 * broken-down calendar (year/mon/mday/hour/min/sec) via sys_time(); the
 * epoch/calendar arithmetic is done here, once, in portable C.
 *
 * Calendar math is Howard Hinnant's public-domain civil<->days algorithm
 * (days relative to 1970-01-01). __days_from_civil / __civil_from_days are
 * exported so the CP/M seam (cpm.c), which only gets days-since-1978 from
 * BDOS 105, can reuse the same conversion.
 * ===================================================================== */
#include <time.h>

struct __sysdt {
  long year; /* full year, e.g. 2026 */
  long mon;  /* 1..12 */
  long mday; /* 1..31 */
  long hour; /* 0..23 */
  long min;  /* 0..59 */
  long sec;  /* 0..59 */
};
extern int sys_time(struct __sysdt *dt); /* 0 = ok, -1 = no clock */

/* days since 1970-01-01 for a Gregorian y-m-d (m,d may be out of range;
   the result stays linear in d, which mktime() relies on to normalize). */
long __days_from_civil(int y, int m, int d) {
  y -= (m <= 2);
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400;                                  /* [0,399] */
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; /* [0,365] */
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          /* [0,146096] */
  return (long)era * 146097 + doe - 719468;
}

/* inverse: days since 1970-01-01 -> y/m/d. */
void __civil_from_days(long z, int *py, int *pm, int *pd) {
  z += 719468;
  int era = (int)((z >= 0 ? z : z - 146096) / 146097);
  int doe = (int)(z - (long)era * 146097);                   /* [0,146096] */
  int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0,399] */
  int y = yoe + era * 400;
  int doy = doe - (365 * yoe + yoe / 4 - yoe / 100); /* [0,365] */
  int mp = (5 * doy + 2) / 153;                      /* [0,11] */
  int d = doy - (153 * mp + 2) / 5 + 1;              /* [1,31] */
  int m = mp + (mp < 10 ? 3 : -9);                   /* [1,12] */
  *py = y + (m <= 2);
  *pm = m;
  *pd = d;
}

static struct tm _tm_buf;
static const char _wday_abbr[7][4] = {"Sun", "Mon", "Tue", "Wed",
                                      "Thu", "Fri", "Sat"};
static const char _mon_abbr[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

time_t time(time_t *timer) {
  struct __sysdt dt;
  if (sys_time(&dt) != 0) {
    if (timer)
      *timer = (time_t)-1;
    return (time_t)-1;
  }
  long days = __days_from_civil((int)dt.year, (int)dt.mon, (int)dt.mday);
  time_t t = days * 86400L + dt.hour * 3600L + dt.min * 60L + dt.sec;
  if (timer)
    *timer = t;
  return t;
}

clock_t clock(void) { return (clock_t)-1; } /* no CPU-time source */

double difftime(time_t end, time_t start) { return (double)(end - start); }

struct tm *gmtime(const time_t *timer) {
  time_t t = *timer;
  long days = t / 86400L;
  long rem = t % 86400L;
  if (rem < 0) {
    rem += 86400L;
    days -= 1;
  }
  int y, m, d;
  __civil_from_days(days, &y, &m, &d);
  _tm_buf.tm_year = y - 1900;
  _tm_buf.tm_mon = m - 1;
  _tm_buf.tm_mday = d;
  _tm_buf.tm_hour = (int)(rem / 3600);
  _tm_buf.tm_min = (int)((rem % 3600) / 60);
  _tm_buf.tm_sec = (int)(rem % 60);
  int wd = (int)((days % 7 + 4) % 7); /* 1970-01-01 was a Thursday (4) */
  if (wd < 0)
    wd += 7;
  _tm_buf.tm_wday = wd;
  _tm_buf.tm_yday = (int)(days - __days_from_civil(y, 1, 1));
  _tm_buf.tm_isdst = 0;
  return &_tm_buf;
}

struct tm *localtime(const time_t *timer) { return gmtime(timer); }

time_t mktime(struct tm *tm) {
  int y = tm->tm_year + 1900;
  int mo = tm->tm_mon; /* 0-based; may be out of range */
  int yadj = mo / 12;
  mo -= yadj * 12;
  y += yadj;
  if (mo < 0) {
    mo += 12;
    y -= 1;
  }
  long days = __days_from_civil(y, mo + 1, tm->tm_mday);
  time_t t =
      days * 86400L + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
  *tm = *gmtime(&t); /* normalize the caller's struct */
  return t;
}

char *asctime(const struct tm *tm) {
  static char buf[32];
  int wd = tm->tm_wday % 7;
  int mo = tm->tm_mon % 12;
  if (wd < 0)
    wd += 7;
  if (mo < 0)
    mo += 12;
  sprintf(buf, "%s %s %2d %02d:%02d:%02d %d\n", _wday_abbr[wd], _mon_abbr[mo],
          tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
  return buf;
}

char *ctime(const time_t *timer) { return asctime(localtime(timer)); }

char *ctime_r(const time_t *timer, char *buf) {
  char *s = asctime(localtime(timer));
  size_t i = 0;
  while (s[i]) {
    buf[i] = s[i];
    i++;
  }
  buf[i] = 0;
  return buf;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
  size_t n = 0;
  char tmp[16];
  const char *p;
#define _PUT(ch)                                                               \
  do {                                                                         \
    if (n + 1 < max)                                                           \
      s[n] = (char)(ch);                                                       \
    n++;                                                                       \
  } while (0)
#define _PUTS(str)                                                             \
  do {                                                                         \
    for (p = (str); *p; p++)                                                   \
      _PUT(*p);                                                                \
  } while (0)
  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      _PUT(*fmt);
      continue;
    }
    fmt++;
    switch (*fmt) {
    case 'Y':
      sprintf(tmp, "%d", tm->tm_year + 1900);
      _PUTS(tmp);
      break;
    case 'y':
      sprintf(tmp, "%02d", (tm->tm_year + 1900) % 100);
      _PUTS(tmp);
      break;
    case 'm':
      sprintf(tmp, "%02d", tm->tm_mon + 1);
      _PUTS(tmp);
      break;
    case 'd':
      sprintf(tmp, "%02d", tm->tm_mday);
      _PUTS(tmp);
      break;
    case 'e':
      sprintf(tmp, "%2d", tm->tm_mday);
      _PUTS(tmp);
      break;
    case 'H':
      sprintf(tmp, "%02d", tm->tm_hour);
      _PUTS(tmp);
      break;
    case 'M':
      sprintf(tmp, "%02d", tm->tm_min);
      _PUTS(tmp);
      break;
    case 'S':
      sprintf(tmp, "%02d", tm->tm_sec);
      _PUTS(tmp);
      break;
    case 'j':
      sprintf(tmp, "%03d", tm->tm_yday + 1);
      _PUTS(tmp);
      break;
    case 'a':
      _PUTS(_wday_abbr[(tm->tm_wday % 7 + 7) % 7]);
      break;
    case 'b':
    case 'h':
      _PUTS(_mon_abbr[(tm->tm_mon % 12 + 12) % 12]);
      break;
    case 'p':
      _PUTS(tm->tm_hour < 12 ? "AM" : "PM");
      break;
    case '%':
      _PUT('%');
      break;
    case '\0':
      fmt--;
      break;
    default:
      _PUT('%');
      _PUT(*fmt);
      break;
    }
  }
#undef _PUT
#undef _PUTS
  if (max)
    s[n < max ? n : max - 1] = '\0';
  return n < max ? n : 0;
}


