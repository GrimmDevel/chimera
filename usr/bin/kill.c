// kill - send signal to process
#include <kernel/xiu_types.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int kill(pid_t pid, int sig);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: kill [-sig] <pid...>\n");
        return 1;
    }

    int sig = SIGTERM;
    int arg_start = 1;

    if (argv[1][0] == '-') {
        if (strcmp(argv[1], "-9") == 0 || strcmp(argv[1], "-KILL") == 0) {
            sig = SIGKILL;
        } else if (strcmp(argv[1], "-15") == 0 || strcmp(argv[1], "-TERM") == 0) {
            sig = SIGTERM;
        } else if (strcmp(argv[1], "-2") == 0 || strcmp(argv[1], "-INT") == 0) {
            sig = SIGINT;
        } else {
            sig = atoi(&argv[1][1]);
            if (sig <= 0) sig = SIGTERM;
        }
        arg_start = 2;
    }

    int ret = 0;
    for (int i = arg_start; i < argc; i++) {
        pid_t pid = (pid_t)atoi(argv[i]);
        if (kill(pid, sig) < 0) {
            printf("kill: %d: no such process or permission denied\n", pid);
            ret = 1;
        }
    }
    return ret;
}
