// seq - print a sequence of numbers
#include <kernel/chimera_types.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    i64 first = 1;
    i64 step = 1;
    i64 last = 1;

    if (argc == 2) {
        last = atoll(argv[1]);
    } else if (argc == 3) {
        first = atoll(argv[1]);
        last = atoll(argv[2]);
    } else if (argc >= 4) {
        first = atoll(argv[1]);
        step = atoll(argv[2]);
        last = atoll(argv[3]);
    } else {
        printf("usage: seq [first [incr]] last\n");
        return 1;
    }

    if (step == 0) {
        printf("seq: zero increment\n");
        return 1;
    }

    if (step > 0) {
        for (i64 i = first; i <= last; i += step) {
            printf("%lld\n", (long long)i);
        }
    } else {
        for (i64 i = first; i >= last; i += step) {
            printf("%lld\n", (long long)i);
        }
    }
    return 0;
}
