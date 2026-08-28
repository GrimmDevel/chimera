#include <kernel/chimera_types.h>

extern i64 write(int fd, const void *buf, usize len);
extern i64 chimera_mkdir(const char *path, u32 mode);
extern usize strlen(const char *s);

void print(const char *s) {
    write(1, s, strlen(s));
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print("usage: mkdir directory\n");
        return 1;
    }
    
    if (chimera_mkdir(argv[1], 0755) < 0) {
        print("mkdir: failed to create directory\n");
        return 1;
    }
    
    return 0;
}
