/*
 * timetest.c -- c68k <time.h> conformance battery.
 *
 * Exercises the calendar/epoch math (gmtime/mktime round-trips against
 * fixed known instants), strftime/asctime formatting, difftime (soft
 * float), and a live time() sanity check over the seam clock (Osiris DOS
 * 2Ah/2Ch, CP/M-68K BDOS 105). Deterministic checks don't depend on the
 * actual RTC value, so they pass identically on both OSes. Prints
 * "TIME PASS n/n". Name kept <= 8 chars for CP/M 8.3.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

static int passes = 0;
static int total = 0;

static void chk(int cond, int id) {
  total++;
  if (cond)
    passes++;
  else
    printf("FAIL: check %d\n", id);
}

int main(void) {
  /* --- 1. Known instant: 2000-01-01 00:00:00 UTC (a Saturday). --- */
  time_t t0 = 946684800L;
  struct tm a = *gmtime(&t0);
  chk(a.tm_year == 100 && a.tm_mon == 0 && a.tm_mday == 1, 1);
  chk(a.tm_hour == 0 && a.tm_min == 0 && a.tm_sec == 0, 2);
  chk(a.tm_wday == 6, 3); /* Saturday */
  chk(a.tm_yday == 0, 4);

  /* --- 2. mktime is the inverse of gmtime. --- */
  struct tm b = a;
  chk(mktime(&b) == t0, 5);

  /* --- 3. Second instant built from fields: 2026-07-18 (a Saturday). --- */
  struct tm c;
  memset(&c, 0, sizeof c);
  c.tm_year = 2026 - 1900;
  c.tm_mon = 6; /* July */
  c.tm_mday = 18;
  time_t t2 = mktime(&c);
  chk(t2 == 1784332800L, 6);
  chk(c.tm_wday == 6, 7); /* mktime normalized wday */

  /* --- 4. Field normalization: Jan 32 == Feb 1. --- */
  struct tm d;
  memset(&d, 0, sizeof d);
  d.tm_year = 2021 - 1900;
  d.tm_mon = 0;
  d.tm_mday = 32;
  mktime(&d);
  chk(d.tm_mon == 1 && d.tm_mday == 1, 8);

  /* --- 5. difftime (returns double via soft float). --- */
  chk(difftime(t2 + 3600, t2) == 3600.0, 9);

  /* --- 6. strftime numeric + names. --- */
  char buf[64];
  struct tm e = *gmtime(&t0);
  strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &e);
  chk(strcmp(buf, "2000-01-01 00:00:00") == 0, 10);
  strftime(buf, sizeof buf, "%a %b %d", &e);
  chk(strcmp(buf, "Sat Jan 01") == 0, 11);

  /* --- 7. asctime canonical form. --- */
  struct tm f = *gmtime(&t0);
  chk(strcmp(asctime(&f), "Sat Jan  1 00:00:00 2000\n") == 0, 12);

  /* --- 8. Live seam clock: readable + fields in range + self-inverse. --- */
  time_t now = time((time_t *)0);
  struct tm g = *gmtime(&now);
  chk(g.tm_mon >= 0 && g.tm_mon <= 11 && g.tm_mday >= 1 && g.tm_mday <= 31, 13);
  chk(g.tm_hour >= 0 && g.tm_hour <= 23 && g.tm_min >= 0 && g.tm_min <= 59 &&
          g.tm_sec >= 0 && g.tm_sec <= 60,
      14);
  struct tm h = g;
  chk(mktime(&h) == now, 15);

  printf("TIME PASS %d/%d\n", passes, total);
  return (passes == total) ? 0 : 1;
}
