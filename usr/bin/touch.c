#include <kernel/xiu_types.h>

extern i64 open(const char *path, int flags, int mode);
extern i64 close(int fd);
extern i64 write(int fd, const void *buf, usize len);
extern usize strlen(const char *s);

static void print(const char *s) {
    write(1, s, strlen(s));
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print("usage: touch <file>...\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int fd = (int)open(argv[i], 0x0200 | 1, 0644);
        if (fd < 0) {
            print("touch: cannot touch '");
            print(argv[i]);
            print("'\n");
            return 1;
        }
        close(fd);
    }

    return 0;
}
