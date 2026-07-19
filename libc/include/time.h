#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

/* c68k ILP32: time_t / clock_t are 32-bit signed (seconds since the
 * 1970-01-01 UTC epoch; good through 2038). No timezone/DST: the seam
 * clock is treated as UTC, so localtime() == gmtime(). */
typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC 1000000L

struct tm {
  int tm_sec;   /* 0..60 */
  int tm_min;   /* 0..59 */
  int tm_hour;  /* 0..23 */
  int tm_mday;  /* 1..31 */
  int tm_mon;   /* 0..11 */
  int tm_year;  /* years since 1900 */
  int tm_wday;  /* 0..6, Sunday = 0 */
  int tm_yday;  /* 0..365 */
  int tm_isdst; /* always 0 here */
};

time_t time(time_t *timer);
clock_t clock(void);
double difftime(time_t end, time_t start);
time_t mktime(struct tm *tm);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
char *asctime(const struct tm *tm);
char *ctime(const time_t *timer);
char *ctime_r(const time_t *timer, char *buf);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

#endif /* _TIME_H */
