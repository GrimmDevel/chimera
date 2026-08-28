// echo - print arguments
#include <kernel/chimera_types.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    bool newline = true;
    bool escapes = false;
    int start = 1;

    while (start < argc && argv[start][0] == '-') {
        if (strcmp(argv[start], "-n") == 0) {
            newline = false;
            start++;
        } else if (strcmp(argv[start], "-e") == 0) {
            escapes = true;
            start++;
        } else {
            break;
        }
    }

    for (int i = start; i < argc; i++) {
        const char *s = argv[i];
        if (escapes) {
            while (*s) {
                if (*s == '\\' && *(s + 1)) {
                    s++;
                    if (*s == 'n') putchar('\n');
                    else if (*s == 't') putchar('\t');
                    else if (*s == 'r') putchar('\r');
                    else if (*s == 'e') putchar('\033');
                    else if (*s == '\\') putchar('\\');
                    else { putchar('\\'); putchar(*s); }
                } else {
                    putchar(*s);
                }
                s++;
            }
        } else {
            printf("%s", s);
        }
        if (i < argc - 1) putchar(' ');
    }
    if (newline) putchar('\n');
    return 0;
}
