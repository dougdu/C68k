#include <locale.h>
#include <limits.h>
#include <string.h>
#include <stddef.h>

/* Only the "C" locale is supported (equivalently "POSIX" and the "" native
 * locale, which is the C locale on these targets). */
static char _c_name[] = "C";

char *setlocale(int category, const char *locale) {
  (void)category;
  if (locale == NULL)
    return _c_name; /* query the current locale */
  if (locale[0] == '\0' || strcmp(locale, "C") == 0 ||
      strcmp(locale, "POSIX") == 0)
    return _c_name;
  return NULL; /* any other locale is unavailable */
}

/* The C-locale lconv: '.' decimal point, everything else empty/unspecified
 * (CHAR_MAX marks a value that is not available in the "C" locale). */
static struct lconv _c_lconv = {
    ".", "", "", "", "", "", "", "", "", "",
    CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
    CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
};

struct lconv *localeconv(void) { return &_c_lconv; }
