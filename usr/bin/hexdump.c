// hexdump - canonical hex+ascii display
#include <kernel/xiu_types.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static void dump_fd(int fd) {
    u8 buf[16];
    u64 offset = 0;
    i64 n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        printf("%08llx  ", (long long)offset);

        for (int i = 0; i < 16; i++) {
            if (i < n) {
                printf("%02x ", buf[i]);
            } else {
                printf("   ");
            }
            if (i == 7) printf(" ");
        }

        printf(" |");
        for (int i = 0; i < n; i++) {
            u8 c = buf[i];
            putchar((c >= 32 && c <= 126) ? c : '.');
        }
        printf("|\n");

        offset += n;
    }
    printf("%08llx\n", (long long)offset);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        dump_fd(0);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            printf("hexdump: cannot open '%s'\n", argv[i]);
            continue;
        }
        dump_fd(fd);
        close(fd);
    }
    return 0;
}
