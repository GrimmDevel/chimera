// ponytail: extern instantiations for wchar ctype functions.
// C99 bare `inline` in Darwin headers doesn't emit external symbols;
// ObjC TUs may reference them as extern calls. This file provides the bodies.
// Must define _DONT_USE_CTYPE_INLINE_ before any system header inclusion.
#define _DONT_USE_CTYPE_INLINE_
#include <sys/types.h>

typedef unsigned int wint_t;
typedef unsigned long wctype_t;

int iswspace(wint_t wc) {
    return (wc == L' ' || wc == L'\t' || wc == L'\n' || wc == L'\r' || wc == L'\f' || wc == L'\v');
}

int iswprint(wint_t wc) {
    // ascii printable range; upgrade when real locale support lands
    return (wc >= 0x20 && wc <= 0x7e);
}

int iswctype(wint_t wc, wctype_t charclass) {
    // ponytail: minimal — only handles space/print for sh expand.c
    (void)charclass;
    return iswprint(wc);
}

