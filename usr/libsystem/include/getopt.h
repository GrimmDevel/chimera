#ifndef _GETOPT_H
#define _GETOPT_H

extern char *optarg;
extern int optind, opterr, optopt, optreset;

int getopt(int argc, char * const argv[], const char *optstring);

#endif
