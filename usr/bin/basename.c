// basename - strip directory and suffix from filenames
#include <kernel/xiu_types.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: basename string [suffix]\n");
        return 1;
    }

    const char *path = argv[1];
    usize len = strlen(path);

    // strip trailing slashes
    while (len > 1 && path[len - 1] == '/') len--;

    const char *base = path;
    for (usize i = 0; i < len; i++) {
        if (path[i] == '/') base = &path[i + 1];
    }

    char res[256];
    usize base_len = (&path[len] >= base) ? (usize)(&path[len] - base) : 0;
    if (base_len >= sizeof(res)) base_len = sizeof(res) - 1;
    memcpy(res, base, base_len);
    res[base_len] = '\0';

    // strip optional suffix
    if (argc > 2) {
        const char *suf = argv[2];
        usize suflen = strlen(suf);
        if (base_len > suflen && strcmp(&res[base_len - suflen], suf) == 0) {
            res[base_len - suflen] = '\0';
        }
    }

    printf("%s\n", res);
    return 0;
}
