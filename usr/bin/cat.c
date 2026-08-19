/* =============================================================================
 * XIU Operating System — cat Utility
 * usr/bin/cat.c
 * ============================================================================= */

#include <kernel/xiu_types.h>

extern i64 write(int fd, const void *buf, usize len);
extern i64 read(int fd, void *buf, usize len);
extern int open(const char *path, int flags, int mode);

void print(const char *s) {
    usize len = 0;
    while(s[len]) len++;
    write(1, s, len);
}

int main(int argc, char *argv[]) {
    if (argc < 2) return 0;
    
    int fd = open(argv[1], 0, 0);
    if (fd < 0) {
        print("cat: file not found\n");
        return 1;
    }
    
    char buf[1024];
    i64 bytes;
    while ((bytes = read(fd, buf, 1024)) > 0) {
        write(1, buf, bytes);
    }
    
    return 0;
}
