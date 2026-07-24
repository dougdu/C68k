#include <getopt.h>
#include <stdio.h>
#include <string.h>

char *optarg = 0;
int optind = 1;
int opterr = 1;
int optopt = 0;

static int optpos = 1; /* position within the current argv element */

int getopt(int argc, char *const argv[], const char *optstring) {
  optarg = 0;
  if (optpos == 1) {
    if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0')
      return -1;
    if (argv[optind][1] == '-' && argv[optind][2] == '\0') {
      optind++;
      return -1;
    }
  }

  int c = (unsigned char)argv[optind][optpos];
  const char *spec = (c == ':') ? 0 : strchr(optstring, c);

  if (!spec) {
    optopt = c;
    if (argv[optind][++optpos] == '\0') {
      optind++;
      optpos = 1;
    }
    if (opterr && optstring[0] != ':')
      fprintf(stderr, "%s: illegal option -- %c\n", argv[0], c);
    return '?';
  }

  if (spec[1] == ':') {
    if (argv[optind][optpos + 1] != '\0') {
      optarg = (char *)&argv[optind][optpos + 1];
      optind++;
    } else if (spec[2] == ':') {
      optarg = 0; /* optional argument, none supplied inline */
      optind++;
    } else {
      if (++optind >= argc) {
        optopt = c;
        optpos = 1;
        if (opterr && optstring[0] != ':')
          fprintf(stderr, "%s: option requires an argument -- %c\n", argv[0], c);
        return (optstring[0] == ':') ? ':' : '?';
      }
      optarg = (char *)argv[optind];
      optind++;
    }
    optpos = 1;
    return c;
  }

  if (argv[optind][++optpos] == '\0') {
    optind++;
    optpos = 1;
  }
  return c;
}

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex) {
  optarg = 0;
  if (optpos == 1 && optind < argc && argv[optind][0] == '-' &&
      argv[optind][1] == '-' && argv[optind][2] != '\0') {
    const char *name = argv[optind] + 2;
    const char *eq = strchr(name, '=');
    size_t namelen = eq ? (size_t)(eq - name) : strlen(name);
    for (int i = 0; longopts && longopts[i].name; i++) {
      if (strlen(longopts[i].name) == namelen &&
          strncmp(longopts[i].name, name, namelen) == 0) {
        optind++;
        if (longindex)
          *longindex = i;
        if (longopts[i].has_arg == required_argument) {
          if (eq)
            optarg = (char *)eq + 1;
          else if (optind < argc)
            optarg = (char *)argv[optind++];
          else {
            optopt = longopts[i].val;
            if (opterr && optstring[0] != ':')
              fprintf(stderr, "%s: option '--%s' requires an argument\n",
                      argv[0], longopts[i].name);
            return (optstring[0] == ':') ? ':' : '?';
          }
        } else if (longopts[i].has_arg == optional_argument) {
          optarg = eq ? (char *)eq + 1 : 0;
        } else if (eq) {
          if (opterr)
            fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n",
                    argv[0], longopts[i].name);
          return '?';
        }
        if (longopts[i].flag) {
          *longopts[i].flag = longopts[i].val;
          return 0;
        }
        return longopts[i].val;
      }
    }
    optind++;
    optopt = 0;
    if (opterr)
      fprintf(stderr, "%s: unrecognized option '--%s'\n", argv[0], name);
    return '?';
  }
  return getopt(argc, argv, optstring);
}
