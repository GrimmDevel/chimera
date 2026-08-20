// head - output first part of files
#include <kernel/xiu_types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void head_fd(int fd, int max_lines) {
    char buf[1024];
    int lines = 0;
    i64 n;

    while (lines < max_lines && (n = read(fd, buf, sizeof(buf))) > 0) {
        for (i64 i = 0; i < n; i++) {
            putchar(buf[i]);
            if (buf[i] == '\n') {
                lines++;
                if (lines >= max_lines) break;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    int max_lines = 10;
    int arg_start = 1;

    if (argc > 1 && argv[1][0] == '-') {
        if (strcmp(argv[1], "-n") == 0 && argc > 2) {
            max_lines = atoi(argv[2]);
            arg_start = 3;
        } else if (argv[1][1] >= '0' && argv[1][1] <= '9') {
            max_lines = atoi(&argv[1][1]);
            arg_start = 2;
        }
    }

    if (arg_start >= argc) {
        head_fd(0, max_lines);
        return 0;
    }

    for (int i = arg_start; i < argc; i++) {
        if (argc - arg_start > 1) {
            printf("==> %s <==\n", argv[i]);
        }
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            printf("head: cannot open '%s'\n", argv[i]);
            continue;
        }
        head_fd(fd, max_lines);
        close(fd);
    }
    return 0;
}
