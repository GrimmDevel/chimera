/* =============================================================================
 * XIU Operating System — User Space C Library
 * usr/libsystem/stdio/vsscanf.c
 *
 * Full POSIX formatting scanner supporting integers, floats, strings, scansets,
 * length modifiers (hh, h, l, ll, z, j, t), and assignment suppression (*).
 * =============================================================================
 */

#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

int vsscanf(const char *str, const char *fmt, va_list ap) {
    if (!str || !fmt) return -1;
    const char *s = str;
    const char *f = fmt;
    int assigned = 0;

    while (*f) {
        if (isspace((unsigned char)*f)) {
            while (isspace((unsigned char)*f)) f++;
            while (isspace((unsigned char)*s)) s++;
            continue;
        }

        if (*f != '%') {
            if (*s != *f) {
                return (*s == '\0' && assigned == 0) ? -1 : assigned;
            }
            s++;
            f++;
            continue;
        }

        f++; // skip '%'

        // assignment suppression
        int suppress = 0;
        if (*f == '*') {
            suppress = 1;
            f++;
        }

        // field width
        int width = 0;
        while (isdigit((unsigned char)*f)) {
            width = width * 10 + (*f - '0');
            f++;
        }
        if (width <= 0) width = 0x7FFFFFFF;

        // length modifier
        int length = 0; // 0=default, 1=hh, 2=h, 3=l, 4=ll, 5=j, 6=z, 7=t, 8=L
        if (*f == 'h') {
            f++;
            if (*f == 'h') { length = 1; f++; }
            else { length = 2; }
        } else if (*f == 'l') {
            f++;
            if (*f == 'l') { length = 4; f++; }
            else { length = 3; }
        } else if (*f == 'j') {
            length = 5; f++;
        } else if (*f == 'z') {
            length = 6; f++;
        } else if (*f == 't') {
            length = 7; f++;
        } else if (*f == 'L') {
            length = 8; f++;
        }

        char spec = *f++;
        if (spec == '\0') break;

        if (spec == '%') {
            if (*s != '%') return assigned;
            s++;
            continue;
        }

        if (spec == 'n') {
            if (!suppress) {
                int *np = va_arg(ap, int *);
                if (np) *np = (int)(s - str);
            }
            continue;
        }

        if (spec != 'c' && spec != '[') {
            while (isspace((unsigned char)*s)) s++;
        }

        if (*s == '\0') {
            return (assigned == 0) ? -1 : assigned;
        }

        if (spec == 'c') {
            int count = (width == 0x7FFFFFFF) ? 1 : width;
            char *dest = suppress ? NULL : va_arg(ap, char *);
            int read_chars = 0;
            while (read_chars < count && *s) {
                if (dest) dest[read_chars] = *s;
                s++;
                read_chars++;
            }
            if (read_chars < count) return assigned;
            if (!suppress) assigned++;
        } else if (spec == 's') {
            char *dest = suppress ? NULL : va_arg(ap, char *);
            int read_chars = 0;
            while (read_chars < width && *s && !isspace((unsigned char)*s)) {
                if (dest) dest[read_chars] = *s;
                s++;
                read_chars++;
            }
            if (read_chars == 0) return assigned;
            if (dest) dest[read_chars] = '\0';
            if (!suppress) assigned++;
        } else if (spec == '[') {
            int invert = 0;
            if (*f == '^') {
                invert = 1;
                f++;
            }
            char scan_table[256];
            memset(scan_table, 0, sizeof(scan_table));
            if (*f == ']') {
                scan_table[(unsigned char)']'] = 1;
                f++;
            }
            while (*f && *f != ']') {
                if (f[1] == '-' && f[2] != ']' && f[2] != '\0') {
                    unsigned char c1 = (unsigned char)f[0];
                    unsigned char c2 = (unsigned char)f[2];
                    for (int c = c1; c <= c2; c++) scan_table[c] = 1;
                    f += 3;
                } else {
                    scan_table[(unsigned char)*f] = 1;
                    f++;
                }
            }
            if (*f == ']') f++;

            char *dest = suppress ? NULL : va_arg(ap, char *);
            int read_chars = 0;
            while (read_chars < width && *s) {
                int match = scan_table[(unsigned char)*s];
                if (invert) match = !match;
                if (!match) break;
                if (dest) dest[read_chars] = *s;
                s++;
                read_chars++;
            }
            if (read_chars == 0) return assigned;
            if (dest) dest[read_chars] = '\0';
            if (!suppress) assigned++;
        } else if (spec == 'd' || spec == 'i' || spec == 'u' || spec == 'x' || spec == 'X' || spec == 'o' || spec == 'p') {
            int base = 10;
            if (spec == 'o') base = 8;
            else if (spec == 'x' || spec == 'X' || spec == 'p') base = 16;
            else if (spec == 'i') base = 0;

            char num_buf[64];
            int ni = 0;
            if (*s == '+' || *s == '-') {
                num_buf[ni++] = *s++;
            }
            if (base == 0 || base == 16) {
                if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                    num_buf[ni++] = *s++;
                    num_buf[ni++] = *s++;
                }
            }
            while (ni + 1 < (int)sizeof(num_buf) && ni < width && isxdigit((unsigned char)*s)) {
                num_buf[ni++] = *s++;
            }
            num_buf[ni] = '\0';
            if (ni == 0 || (ni == 1 && (num_buf[0] == '+' || num_buf[0] == '-'))) {
                return assigned;
            }

            char *endp = NULL;
            int is_unsigned = (spec == 'u' || spec == 'x' || spec == 'X' || spec == 'o' || spec == 'p');
            if (is_unsigned) {
                uint64_t val = (uint64_t)strtoull(num_buf, &endp, base);
                if (!suppress) {
                    if (length == 1) *(unsigned char *)va_arg(ap, unsigned int *) = (unsigned char)val;
                    else if (length == 2) *(unsigned short *)va_arg(ap, unsigned int *) = (unsigned short)val;
                    else if (length == 3) *(unsigned long *)va_arg(ap, unsigned long *) = (unsigned long)val;
                    else if (length == 4) *(unsigned long long *)va_arg(ap, unsigned long long *) = val;
                    else if (spec == 'p') *(void **)va_arg(ap, void **) = (void *)(uintptr_t)val;
                    else *(unsigned int *)va_arg(ap, unsigned int *) = (unsigned int)val;
                    assigned++;
                }
            } else {
                int64_t val = (int64_t)strtoll(num_buf, &endp, base);
                if (!suppress) {
                    if (length == 1) *(signed char *)va_arg(ap, int *) = (signed char)val;
                    else if (length == 2) *(short *)va_arg(ap, int *) = (short)val;
                    else if (length == 3) *(long *)va_arg(ap, long *) = (long)val;
                    else if (length == 4) *(long long *)va_arg(ap, long long *) = val;
                    else *(int *)va_arg(ap, int *) = (int)val;
                    assigned++;
                }
            }
        } else if (spec == 'f' || spec == 'F' || spec == 'e' || spec == 'E' || spec == 'g' || spec == 'G') {
            char num_buf[64];
            int ni = 0;
            if (*s == '+' || *s == '-') num_buf[ni++] = *s++;
            while (ni + 1 < (int)sizeof(num_buf) && ni < width && (isdigit((unsigned char)*s) || *s == '.' || *s == 'e' || *s == 'E' || *s == '+' || *s == '-')) {
                num_buf[ni++] = *s++;
            }
            num_buf[ni] = '\0';
            char *endp = NULL;
            double val = strtod(num_buf, &endp);
            if (endp == num_buf) return assigned;
            if (!suppress) {
                if (length == 3) *(double *)va_arg(ap, double *) = val;
                else if (length == 8) *(long double *)va_arg(ap, long double *) = (long double)val;
                else *(float *)va_arg(ap, float *) = (float)val;
                assigned++;
            }
        }
    }

    return assigned;
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int res = vsscanf(str, fmt, ap);
    va_end(ap);
    return res;
}
