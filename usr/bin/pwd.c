#include <kernel/chimera_types.h>

extern i64 write(int fd, const void *buf, usize len);
extern i64 chimera_getcwd(char *buf, usize size);
extern usize strlen(const char *s);

void print(const char *s) {
    write(1, s, strlen(s));
}

int main() {
    char buf[1024];
    if (chimera_getcwd(buf, 1024) < 0) {
        print("/\n"); // fallback
        return 0;
    }
    print(buf);
    print("\n");
    return 0;
}
