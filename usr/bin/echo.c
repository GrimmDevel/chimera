/* =============================================================================
 * XIU Operating System — echo Utility
 * usr/bin/echo.c
 * ============================================================================= */

#include <kernel/xiu_types.h>

extern i64 write(int fd, const void *buf, usize len);

void print(const char *s) {
    usize len = 0;
    while(s[len]) len++;
    write(1, s, len);
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        print(argv[i]);
        if (i < argc - 1) print(" ");
    }
    print("\n");
    return 0;
}
