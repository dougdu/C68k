/* POSIX TZ handling: with no TZ the RTC is treated as UTC (localtime==gmtime);
 * with TZ set (Osiris) the RTC is local and UTC is derived from it.  Cross-OS:
 * the TZ-set assertions are guarded by `have` (true only where setenv/getenv
 * work, i.e. Osiris), so CP/M -- which has no environment -- passes them
 * trivially and the count matches.  Prints "TZTEST PASS n/n". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int total, pass;
#define CHECK(c)                                                               \
  do {                                                                         \
    total++;                                                                   \
    if (c)                                                                     \
      pass++;                                                                  \
    else                                                                       \
      printf("FAIL line %d\n", __LINE__);                                      \
  } while (0)

static time_t make(int year, int mon0, int mday, int hour, int isdst) {
  struct tm t;
  memset(&t, 0, sizeof t);
  t.tm_year = year - 1900;
  t.tm_mon = mon0;
  t.tm_mday = mday;
  t.tm_hour = hour;
  t.tm_isdst = isdst;
  return mktime(&t);
}

int main(void) {
  /* Default (no TZ): localtime() == gmtime() -- holds on both OSes. */
  unsetenv("TZ");
  tzset();
  {
    time_t s = make(2021, 0, 15, 12, 0);
    struct tm g = *gmtime(&s);
    struct tm l = *localtime(&s);
    CHECK(g.tm_hour == l.tm_hour && g.tm_mday == l.tm_mday && l.tm_isdst == 0);
  }

  /* TZ set (Osiris only). */
  setenv("TZ", "EST5EDT", 1);
  tzset();
  char *tz = getenv("TZ");
  int have = tz && !strcmp(tz, "EST5EDT");

  CHECK(!have || timezone == 18000);
  CHECK(!have || daylight == 1);
  CHECK(!have || (tzname[0] && !strcmp(tzname[0], "EST")));

  /* Winter: 2021-01-15 12:00 EST == 17:00 UTC, no DST. */
  {
    time_t s = make(2021, 0, 15, 12, 0);
    struct tm g = *gmtime(&s);
    CHECK(!have || g.tm_hour == 17);
    struct tm l = *localtime(&s);
    CHECK(!have || (l.tm_hour == 12 && l.tm_isdst == 0));
    char z[8];
    struct tm lt = *localtime(&s);
    strftime(z, sizeof z, "%z", &lt);
    CHECK(!have || !strcmp(z, "-0500"));
  }

  /* Summer: 2021-07-15 12:00 EDT == 16:00 UTC, DST in effect. */
  {
    time_t s = make(2021, 6, 15, 12, -1);
    struct tm g = *gmtime(&s);
    CHECK(!have || g.tm_hour == 16);
    struct tm l = *localtime(&s);
    CHECK(!have || (l.tm_hour == 12 && l.tm_isdst == 1));
    char z[8];
    struct tm lt = *localtime(&s);
    strftime(z, sizeof z, "%Z", &lt);
    CHECK(!have || !strcmp(z, "EDT"));
  }

  unsetenv("TZ");
  tzset();
  printf("TZTEST %s %d/%d\n", pass == total ? "PASS" : "FAIL", pass, total);
  return pass == total ? 0 : 1;
}
