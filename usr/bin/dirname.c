// dirname - strip last component from filename
#include <kernel/xiu_types.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: dirname string\n");
        return 1;
    }

    const char *path = argv[1];
    usize len = strlen(path);

    // strip trailing slashes
    while (len > 1 && path[len - 1] == '/') len--;

    if (len == 1 && path[0] == '/') {
        printf("/\n");
        return 0;
    }

    const char *last_slash = NULL;
    for (usize i = 0; i < len; i++) {
        if (path[i] == '/') last_slash = &path[i];
    }

    if (!last_slash) {
        printf(".\n");
        return 0;
    }

    if (last_slash == path) {
        printf("/\n");
        return 0;
    }

    char res[256];
    usize dir_len = (usize)(last_slash - path);
    if (dir_len >= sizeof(res)) dir_len = sizeof(res) - 1;
    memcpy(res, path, dir_len);
    res[dir_len] = '\0';

    printf("%s\n", res);
    return 0;
}
