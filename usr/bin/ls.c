#include <kernel/xiu_types.h>
#include "../libsystem/include/dirent.h"

extern i64 write(int fd, const void *buf, usize len);
extern usize strlen(const char *s);

void print(const char *s) {
    write(1, s, strlen(s));
}

int main(int argc, char *argv[]) {
    const char *path = ".";
    if (argc > 1) path = argv[1];
    
    DIR *dir = opendir(path);
    if (!dir) {
        print("ls: cannot open directory\n");
        return 1;
    }
    
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        print(de->d_name);
        print("  ");
    }
    print("\n");
    
    closedir(dir);
    return 0;
}
