#define _DONT_USE_CTYPE_INLINE_ 1
#define _EXTERNALIZE_CTYPE_INLINES_ 1
#include <ctype.h>

int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isdigit(int c) { return (c >= '0' && c <= '9'); }
int isspace(int c) { return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'); }
int isblank(int c) { return (c == ' ' || c == '\t'); }
int isascii(int c) { return (unsigned int)c <= 127; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c; }
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c; }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int iscntrl(int c) { return ((unsigned int)c < 32) || (c == 127); }
int isgraph(int c) { return c > 32 && c < 127; }
int islower(int c) { return (c >= 'a' && c <= 'z'); }
int isprint(int c) { return c >= 32 && c < 127; }
int ispunct(int c) { return isprint(c) && !isalnum(c) && !isspace(c); }
int isupper(int c) { return (c >= 'A' && c <= 'Z'); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int toascii(int c) { return (c & 0x7F); }
int _tolower(int c) { return tolower(c); }
int _toupper(int c) { return toupper(c); }
