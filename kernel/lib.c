/* =============================================================================
 * XIU Operating System — Minimal Freestanding Lib
 * kernel/lib.c
 * ============================================================================= */

#include <kernel/xiu_types.h>

void *memset(void *s, int c, usize n) {
    u8 *p = (u8 *)s;
    while (n--) *p++ = (u8)c;
    return s;
}

void *memcpy(void *dest, const void *src, usize n) {
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;
    while (n--) *d++ = *s++;
    return dest;
}

char *strncpy(char *dest, const char *src, usize n) {
    usize i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

usize strlen(const char *s) {
    usize len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int memcmp(const void *s1, const void *s2, usize n) {
    const u8 *p1 = (const u8 *)s1;
    const u8 *p2 = (const u8 *)s2;
    while (n--) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

