// yes - output string repeatedly
#include <kernel/xiu_types.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    const char *msg = (argc > 1) ? argv[1] : "y";
    while (1) {
        if (puts(msg) < 0) break;
    }
    return 0;
}
