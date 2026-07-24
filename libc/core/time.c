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

/* ---- POSIX TZ support -------------------------------------------------
 * The seam clock (Osiris/CP-M RTC) reads LOCAL wall time.  With no TZ set the
 * library treats that as UTC (localtime()==gmtime()) -- byte-identical to the
 * old behaviour.  When the TZ environment variable is present (Osiris only;
 * CP/M has no environment) it is parsed as a POSIX zone
 *   std offset [ dst [offset] [ ,start[/time],end[/time] ] ]
 * and the RTC is interpreted as local time, from which UTC is computed.  The
 * convention used internally: `off` = seconds to ADD to local time to reach
 * UTC (i.e. west of UTC is positive), matching the POSIX `offset` field. */
long timezone = 0; /* seconds: UTC = local_standard + timezone */
int daylight = 0;
static char _tzn_std[8] = "UTC";
static char _tzn_dst[8] = "";
char *tzname[2] = {_tzn_std, _tzn_dst};

struct __tzrule {
  char type;           /* 'M', 'J', 'D', or 0 (none) */
  int mon, week, wday; /* for 'M': month 1-12, week 1-5, weekday 0-6 (Sun=0) */
  int yday;            /* for 'J' (1-365) or 'D' (0-365) */
  long secs;           /* seconds after local midnight; default 02:00:00 */
};
static struct {
  int inited;
  int has_dst;
  long std_off, dst_off; /* add to local to get UTC */
  struct __tzrule start, end;
} _tz;

static const char *_tz_off(const char *p, long *out) {
  int sign = 1;
  if (*p == '+')
    p++;
  else if (*p == '-') {
    sign = -1;
    p++;
  }
  long h = 0, m = 0, s = 0;
  while (*p >= '0' && *p <= '9')
    h = h * 10 + (*p++ - '0');
  if (*p == ':') {
    p++;
    while (*p >= '0' && *p <= '9')
      m = m * 10 + (*p++ - '0');
    if (*p == ':') {
      p++;
      while (*p >= '0' && *p <= '9')
        s = s * 10 + (*p++ - '0');
    }
  }
  *out = sign * (h * 3600 + m * 60 + s);
  return p;
}

static const char *_tz_name(const char *p, char *out) {
  int n = 0;
  if (*p == '<') {
    p++;
    while (*p && *p != '>') {
      if (n < 7)
        out[n++] = *p;
      p++;
    }
    if (*p == '>')
      p++;
  } else {
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
      if (n < 7)
        out[n++] = *p;
      p++;
    }
  }
  out[n] = 0;
  return p;
}

static const char *_tz_rule(const char *p, struct __tzrule *r) {
  r->secs = 7200; /* default 02:00:00 local */
  if (*p == 'M') {
    p++;
    r->type = 'M';
    r->mon = r->week = r->wday = 0;
    while (*p >= '0' && *p <= '9')
      r->mon = r->mon * 10 + (*p++ - '0');
    if (*p == '.')
      p++;
    while (*p >= '0' && *p <= '9')
      r->week = r->week * 10 + (*p++ - '0');
    if (*p == '.')
      p++;
    while (*p >= '0' && *p <= '9')
      r->wday = r->wday * 10 + (*p++ - '0');
  } else if (*p == 'J') {
    p++;
    r->type = 'J';
    r->yday = 0;
    while (*p >= '0' && *p <= '9')
      r->yday = r->yday * 10 + (*p++ - '0');
  } else {
    r->type = 'D';
    r->yday = 0;
    while (*p >= '0' && *p <= '9')
      r->yday = r->yday * 10 + (*p++ - '0');
  }
  if (*p == '/') {
    p++;
    long t;
    p = _tz_off(p, &t);
    r->secs = t;
  }
  return p;
}

void tzset(void) {
  _tz.inited = 1;
  _tz.has_dst = 0;
  _tz.std_off = _tz.dst_off = 0;
  _tz.start.type = _tz.end.type = 0;
  strcpy(_tzn_std, "UTC");
  _tzn_dst[0] = 0;
  const char *tz = getenv("TZ");
  if (!tz || !*tz) {
    timezone = 0;
    daylight = 0;
    return;
  }
  char nm[16];
  const char *p = _tz_name(tz, nm);
  if (!nm[0]) { /* malformed -> stay UTC */
    timezone = 0;
    daylight = 0;
    return;
  }
  strcpy(_tzn_std, nm);
  long off = 0;
  p = _tz_off(p, &off);
  _tz.std_off = off; /* POSIX: local + off = UTC */
  if (*p) {          /* a DST zone name follows */
    p = _tz_name(p, nm);
    strcpy(_tzn_dst, nm);
    _tz.has_dst = 1;
    if (*p && *p != ',') {
      long doff;
      p = _tz_off(p, &doff);
      _tz.dst_off = doff;
    } else {
      _tz.dst_off = _tz.std_off - 3600; /* default: one hour less */
    }
    if (*p == ',') {
      p++;
      p = _tz_rule(p, &_tz.start);
      if (*p == ',') {
        p++;
        p = _tz_rule(p, &_tz.end);
      }
    } else {
      /* No rules given -> US default: DST 2nd Sun Mar .. 1st Sun Nov, 02:00. */
      _tz.start.type = 'M';
      _tz.start.mon = 3;
      _tz.start.week = 2;
      _tz.start.wday = 0;
      _tz.start.secs = 7200;
      _tz.end.type = 'M';
      _tz.end.mon = 11;
      _tz.end.week = 1;
      _tz.end.wday = 0;
      _tz.end.secs = 7200;
    }
  }
  timezone = _tz.std_off;
  daylight = _tz.has_dst;
}

static void tz_ensure(void) {
  if (!_tz.inited)
    tzset();
}

/* days-since-epoch of a DST transition rule in `year`. */
static long tz_trans_days(const struct __tzrule *r, int year) {
  if (r->type == 'M') {
    int first = (int)(((__days_from_civil(year, r->mon, 1) % 7) + 4) % 7);
    if (first < 0)
      first += 7; /* 0 = Sunday */
    int day = 1 + ((r->wday - first + 7) % 7) + (r->week - 1) * 7;
    static const int mdays[13] = {0,  31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
    int dim = mdays[r->mon];
    if (r->mon == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
      dim = 29;
    while (day > dim)
      day -= 7; /* week 5 or overflow -> last occurrence */
    return __days_from_civil(year, r->mon, day);
  }
  int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  int doy = (r->type == 'J') ? (r->yday - 1 + ((leap && r->yday >= 60) ? 1 : 0))
                             : r->yday;
  return __days_from_civil(year, 1, 1) + doy;
}

/* Is a LOCAL instant (seconds since epoch, and its calendar year) in DST? */
static int tz_isdst_local(long localsec, int year) {
  if (!_tz.has_dst)
    return 0;
  long s = tz_trans_days(&_tz.start, year) * 86400L + _tz.start.secs;
  long e = tz_trans_days(&_tz.end, year) * 86400L + _tz.end.secs;
  if (s <= e)
    return localsec >= s && localsec < e;
  return localsec >= s || localsec < e; /* southern hemisphere */
}

/* Offset (seconds to add to local to reach UTC) for a local instant. Exported
 * so stat.c can convert local FAT timestamps to a UTC time_t consistently. */
long __tz_local_offset(long localsec, int year) {
  tz_ensure();
  return tz_isdst_local(localsec, year) ? _tz.dst_off : _tz.std_off;
}

time_t time(time_t *timer) {
  struct __sysdt dt;
  if (sys_time(&dt) != 0) {
    if (timer)
      *timer = (time_t)-1;
    return (time_t)-1;
  }
  tz_ensure();
  long days = __days_from_civil((int)dt.year, (int)dt.mon, (int)dt.mday);
  long local = days * 86400L + dt.hour * 3600L + dt.min * 60L + dt.sec;
  time_t t = (time_t)(local + __tz_local_offset(local, (int)dt.year));
  if (timer)
    *timer = t;
  return t;
}

clock_t clock(void) { return (clock_t)-1; } /* no CPU-time source */

/* C11 timespec_get: only TIME_UTC is supported, at 1-second resolution. */
int timespec_get(struct timespec *ts, int base) {
  if (base != TIME_UTC)
    return 0;
  ts->tv_sec = time(0);
  ts->tv_nsec = 0;
  return base;
}

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

struct tm *localtime(const time_t *timer) {
  tz_ensure();
  time_t utc = *timer;
  /* Two-pass: approximate the local instant with the standard offset, test the
     DST rule, then break down with the resolved offset. */
  time_t guess = utc - _tz.std_off;
  long gdays = guess / 86400L;
  if (guess % 86400L < 0)
    gdays -= 1;
  int gy, gm, gd;
  __civil_from_days(gdays, &gy, &gm, &gd);
  int dst = tz_isdst_local((long)guess, gy);
  long off = dst ? _tz.dst_off : _tz.std_off;
  time_t loc = utc - off;
  struct tm *r = gmtime(&loc); /* fills _tm_buf with the local breakdown */
  r->tm_isdst = (dst && _tz.has_dst) ? 1 : 0;
  return r;
}

time_t mktime(struct tm *tm) {
  tz_ensure();
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
  long local =
      days * 86400L + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
  int dst = (tm->tm_isdst > 0)   ? 1
            : (tm->tm_isdst == 0) ? 0
                                  : tz_isdst_local(local, y);
  long off = dst ? _tz.dst_off : _tz.std_off;
  time_t t = (time_t)(local + off);
  *tm = *localtime(&t); /* normalize the caller's struct */
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
  tz_ensure();
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
    case 'Z': {
      const char *z = (tm->tm_isdst > 0) ? tzname[1] : tzname[0];
      if (z && *z)
        _PUTS(z);
      break;
    }
    case 'z': {
      long off = (tm->tm_isdst > 0) ? _tz.dst_off : _tz.std_off;
      long disp = -off; /* %z is east-of-UTC positive */
      char sgn = disp < 0 ? '-' : '+';
      long a = disp < 0 ? -disp : disp;
      sprintf(tmp, "%c%02ld%02ld", sgn, a / 3600, (a % 3600) / 60);
      _PUTS(tmp);
      break;
    }
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


