#ifndef _CTYPE_H
#define _CTYPE_H

static inline int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }
static inline int isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline int isalnum(int c) { return isalpha(c) || isdigit(c); }
static inline int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int toupper(int c) { return islower(c) ? (c - 'a' + 'A') : c; }
static inline int tolower(int c) { return isupper(c) ? (c - 'A' + 'a') : c; }
static inline int isgraph(int c) { return c > 0x20 && c < 0x7f; }
static inline int isprint(int c) { return c >= 0x20 && c < 0x7f; }
static inline int ispunct(int c) { return isgraph(c) && !isalnum(c); }
static inline int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static inline int iscntrl(int c) { return (c >= 0 && c < 0x20) || (c == 0x7f); }
static inline int isblank(int c) { return c == ' ' || c == '\t'; }

#define _isspace isspace
#define _isdigit isdigit
#define _isalpha isalpha
#define _isalnum isalnum
#define _isupper isupper
#define _islower islower
#define _isgraph isgraph
#define _isprint isprint
#define _ispunct ispunct
#define _isxdigit isxdigit
#define _iscntrl iscntrl
#define _isblank isblank

#endif
