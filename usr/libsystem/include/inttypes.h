#ifndef _INTTYPES_H
#define _INTTYPES_H
#include <kernel/xiu_types.h>

#define PRId64 "lld"
#define PRIu64 "llu"
#define PRIx64 "llx"
#define PRIdMAX "lld"
#define PRIuMAX "llu"
#define PRIxMAX "llx"

#ifndef _INTMAX_T
#define _INTMAX_T
#ifdef __INTMAX_TYPE__
typedef __INTMAX_TYPE__ intmax_t;
#else
typedef i64 intmax_t;
#endif
#endif

#ifndef _UINTMAX_T
#define _UINTMAX_T
#ifdef __UINTMAX_TYPE__
typedef __UINTMAX_TYPE__ uintmax_t;
#else
typedef u64 uintmax_t;
#endif
#endif

intmax_t strtoimax(const char *nptr, char **endptr, int base);

#endif
