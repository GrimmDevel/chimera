// uname - print system information
#include <kernel/xiu_types.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    bool opt_s = false; // sysname
    bool opt_r = false; // release
    bool opt_v = false; // version
    bool opt_m = false; // machine
    bool opt_a = false; // all

    if (argc < 2) {
        opt_s = true;
    } else {
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-') {
                const char *p = &argv[i][1];
                while (*p) {
                    if (*p == 'a') opt_a = true;
                    else if (*p == 's') opt_s = true;
                    else if (*p == 'r') opt_r = true;
                    else if (*p == 'v') opt_v = true;
                    else if (*p == 'm') opt_m = true;
                    p++;
                }
            }
        }
    }

    if (opt_a) {
        opt_s = opt_r = opt_v = opt_m = true;
    }

    bool first = true;
    if (opt_s) {
        printf("%sXIU", first ? "" : " ");
        first = false;
    }
    if (opt_r) {
        printf("%s0.1.0", first ? "" : " ");
        first = false;
    }
    if (opt_v) {
        printf("%sDarwin/Mach Kernel x86_64", first ? "" : " ");
        first = false;
    }
    if (opt_m) {
        printf("%sx86_64", first ? "" : " ");
        first = false;
    }
    printf("\n");
    return 0;
}
