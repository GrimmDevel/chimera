// cat - concatenate and print files
#include <kernel/xiu_types.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void cat_fd(int fd) {
    char buf[4096];
    i64 n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        cat_fd(0);
        return 0;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            cat_fd(0);
            continue;
        }

        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            printf("cat: %s: No such file or directory\n", argv[i]);
            ret = 1;
            continue;
        }
        cat_fd(fd);
        close(fd);
    }
    return ret;
}
