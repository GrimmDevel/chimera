/* =============================================================================
 * XIU Operating System — User Space C Library
 * usr/libsystem/string/strcasecmp.c
 * ============================================================================= */

#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>

int strcasecmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    const unsigned char *us1 = (const unsigned char *)s1;
    const unsigned char *us2 = (const unsigned char *)s2;
    while (*us1 && *us2) {
        int c1 = tolower(*us1);
        int c2 = tolower(*us2);
        if (c1 != c2) return c1 - c2;
        us1++; us2++;
    }
    return tolower(*us1) - tolower(*us2);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    const unsigned char *us1 = (const unsigned char *)s1;
    const unsigned char *us2 = (const unsigned char *)s2;
    while (n && *us1 && *us2) {
        int c1 = tolower(*us1);
        int c2 = tolower(*us2);
        if (c1 != c2) return c1 - c2;
        us1++; us2++;
        n--;
    }
    return n ? (tolower(*us1) - tolower(*us2)) : 0;
}

int strcoll(const char *s1, const char *s2) {
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    return strcmp(s1, s2);
}

int strcoll_l(const char *s1, const char *s2, void *loc) {
    (void)loc;
    return strcoll(s1, s2);
}
