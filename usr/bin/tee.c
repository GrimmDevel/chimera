// tee - read from standard input and write to standard output and files
#include <kernel/xiu_types.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_TEE_FILES 32

int main(int argc, char *argv[]) {
    int fds[MAX_TEE_FILES];
    int num_fds = 0;
    int flags = O_WRONLY | O_CREAT | O_TRUNC;

    int arg = 1;
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        arg = 2;
    }

    for (int i = arg; i < argc && num_fds < MAX_TEE_FILES; i++) {
        int fd = open(argv[i], flags, 0644);
        if (fd >= 0) {
            fds[num_fds++] = fd;
        } else {
            printf("tee: cannot open '%s'\n", argv[i]);
        }
    }

    char buf[4096];
    i64 n;
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
        for (int i = 0; i < num_fds; i++) {
            write(fds[i], buf, n);
        }
    }

    for (int i = 0; i < num_fds; i++) {
        close(fds[i]);
    }
    return 0;
}
