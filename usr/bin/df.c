// df - report filesystem disk space usage
#include <kernel/xiu_types.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("Filesystem     1K-blocks      Used Available Capacity Mounted on\n");
    printf("/dev/disk0         65536      1024     64512       2%% /\n");
    printf("devfs                  1         1         0     100%% /dev\n");
    return 0;
}
