/* =============================================================================
 * Chimera Operating System — User Space C Library
 * usr/libsystem/locale/mbrtowc.c
 *
 * Real UTF-8 and multibyte to wide character decoder.
 * =============================================================================
 */

#include <wchar.h>
#include <stdlib.h>
#include <errno.h>

size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps) {
    (void)ps;
    if (!s) return 0;
    if (n == 0) return (size_t)-2;
    unsigned char c = (unsigned char)*s;
    if (c == 0) {
        if (pwc) *pwc = 0;
        return 0;
    }
    if (c < 0x80) {
        if (pwc) *pwc = c;
        return 1;
    } else if ((c & 0xE0) == 0xC0) {
        if (n < 2) return (size_t)-2;
        if ((s[1] & 0xC0) != 0x80) { errno = EILSEQ; return (size_t)-1; }
        if (pwc) *pwc = ((c & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    } else if ((c & 0xF0) == 0xE0) {
        if (n < 3) return (size_t)-2;
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) { errno = EILSEQ; return (size_t)-1; }
        if (pwc) *pwc = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    } else if ((c & 0xF8) == 0xF0) {
        if (n < 4) return (size_t)-2;
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) { errno = EILSEQ; return (size_t)-1; }
        if (pwc) *pwc = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }
    errno = EILSEQ;
    return (size_t)-1;
}

size_t mbrtowc_l(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps, void *loc) {
    (void)loc;
    return mbrtowc(pwc, s, n, ps);
}

size_t mbrlen(const char *s, size_t n, mbstate_t *ps) {
    return mbrtowc(NULL, s, n, ps);
}

size_t mbrlen_l(const char *s, size_t n, mbstate_t *ps, void *loc) {
    (void)loc;
    return mbrtowc(NULL, s, n, ps);
}

size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps) {
    (void)ps;
    if (!s) return 1;
    if ((unsigned int)wc < 0x80) {
        s[0] = (char)wc;
        return 1;
    } else if ((unsigned int)wc < 0x800) {
        s[0] = (char)(0xC0 | ((wc >> 6) & 0x1F));
        s[1] = (char)(0x80 | (wc & 0x3F));
        return 2;
    } else if ((unsigned int)wc < 0x10000) {
        s[0] = (char)(0xE0 | ((wc >> 12) & 0x0F));
        s[1] = (char)(0x80 | ((wc >> 6) & 0x3F));
        s[2] = (char)(0x80 | (wc & 0x3F));
        return 3;
    } else {
        s[0] = (char)(0xF0 | ((wc >> 18) & 0x07));
        s[1] = (char)(0x80 | ((wc >> 12) & 0x3F));
        s[2] = (char)(0x80 | ((wc >> 6) & 0x3F));
        s[3] = (char)(0x80 | (wc & 0x3F));
        return 4;
    }
}

size_t wcrtomb_l(char *s, wchar_t wc, mbstate_t *ps, void *loc) {
    (void)loc;
    return wcrtomb(s, wc, ps);
}

int wctomb(char *s, wchar_t wc) {
    if (!s) return 0;
    size_t r = wcrtomb(s, wc, NULL);
    if (r == (size_t)-1) return -1;
    return (int)r;
}

