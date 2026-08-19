#ifndef _WCTYPE_H
#define _WCTYPE_H
#include <sys/types.h>
typedef unsigned int wint_t;
typedef unsigned int wctype_t;
static inline int iswprint(wint_t c) { return c >= 0x20 && c < 0x7f; }
static inline int iswblank(wint_t c) { return c == ' ' || c == '\t'; }
static inline int iswspace(wint_t c) { return c == ' ' || (c >= 0x09 && c <= 0x0D); }
static inline wctype_t wctype(const char *name) { return 0; }
static inline int iswctype(wint_t wc, wctype_t desc) { return 0; }
#endif
