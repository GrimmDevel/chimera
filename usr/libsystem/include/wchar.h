#ifndef XIU_WCHAR_H
#define XIU_WCHAR_H

#include <sys/types.h>

typedef int mbstate_t;

usize mbrlen(const char *s, usize n, mbstate_t *ps);
usize mbrtowc(wchar_t *pwc, const char *s, usize n, mbstate_t *ps);
usize mbsrtowcs(wchar_t *dest, const char **src, usize len, mbstate_t *ps);
wchar_t *wcschr(const wchar_t *s, wchar_t c);

#endif
