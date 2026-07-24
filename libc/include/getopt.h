#ifndef _GETOPT_H
#define _GETOPT_H

extern char *optarg; /* argument to the current option */
extern int optind;   /* index of the next argv element to process */
extern int opterr;   /* nonzero: print an error message on a bad option */
extern int optopt;   /* the offending option character */

int getopt(int argc, char *const argv[], const char *optstring);

struct option {
  const char *name;
  int has_arg;
  int *flag;
  int val;
};

#define no_argument 0
#define required_argument 1
#define optional_argument 2

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex);

#endif /* _GETOPT_H */
