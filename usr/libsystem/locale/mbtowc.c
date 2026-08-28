/* =============================================================================
 * Chimera Operating System — User Space C Library
 * usr/libsystem/locale/mbtowc.c
 * ============================================================================= */

#include <wchar.h>
#include <stdlib.h>

extern size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);

int mbtowc(wchar_t *pwc, const char *s, size_t n) {
    if (!s) return 0;
    size_t r = mbrtowc(pwc, s, n, NULL);
    if (r == (size_t)-1 || r == (size_t)-2) return -1;
    return (int)r;
}

int mbtowc_l(wchar_t *pwc, const char *s, size_t n, void *loc) {
    (void)loc;
    return mbtowc(pwc, s, n);
}
