#ifndef XIU_STDLIB_H
#define XIU_STDLIB_H

#include <kernel/xiu_types.h>

void *malloc(usize size);
void *calloc(usize nmemb, usize size);
void *realloc(void *ptr, usize size);
void free(void *ptr);
void exit(int status);
long strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
int atoi(const char *s);
long long atoll(const char *s);
int abs(int j);
int atexit(void (*func)(void));
void abort(void);
void qsort(void *base, usize nmemb, usize size, int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, usize nmemb, usize size, int (*compar)(const void *, const void *));
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
char *realpath(const char *path, char *resolved_path);

#define _exit exit
#define alloca __builtin_alloca

#endif
