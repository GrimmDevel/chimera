// sleep - delay execution
#include <kernel/xiu_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: sleep <seconds>\n");
        return 1;
    }

    int sec = atoi(argv[1]);
    if (sec <= 0) return 0;

    // calibrate loop or sleep
    for (int s = 0; s < sec; s++) {
        for (volatile int i = 0; i < 5000000; i++) {
            if ((i & 0x7FFF) == 0) sched_yield();
        }
    }
    return 0;
}
