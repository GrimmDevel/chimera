#include <sys/cdefs.h>
#include <ctype.h>
#include <runetype.h>

int ___maskrune(__ct_rune_t c, unsigned long f) {
    return (c < 0 || c == -1) ? 0 : ((_DefaultRuneLocale.__runetype[c & 0xff] & f) != 0);
}

unsigned long ___runetype(__ct_rune_t c) {
    if (c < 0 || c == -1) return 0;
    return _DefaultRuneLocale.__runetype[c & 0xff];
}

int ___tolower(__ct_rune_t c) {
    if (c < 0 || c == -1) return c;
    return _DefaultRuneLocale.__maplower[c & 0xff];
}

int ___toupper(__ct_rune_t c) {
    if (c < 0 || c == -1) return c;
    return _DefaultRuneLocale.__mapupper[c & 0xff];
}
