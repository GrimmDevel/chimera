// chmod - change file mode
#include <kernel/xiu_types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int chmod(const char *path, mode_t mode);

static u32 parse_octal(const char *s) {
    u32 mode = 0;
    while (*s >= '0' && *s <= '7') {
        mode = (mode << 3) | (*s - '0');
        s++;
    }
    return mode;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("usage: chmod <mode> <file...>\n");
        return 1;
    }

    const char *modestr = argv[1];
    u32 mode = 0;

    if (modestr[0] >= '0' && modestr[0] <= '7') {
        mode = parse_octal(modestr);
    } else if (strcmp(modestr, "+x") == 0) {
        mode = 0755;
    } else if (strcmp(modestr, "-x") == 0) {
        mode = 0644;
    } else {
        printf("chmod: invalid mode '%s'\n", modestr);
        return 1;
    }

    int ret = 0;
    for (int i = 2; i < argc; i++) {
        if (chmod(argv[i], mode) < 0) {
            printf("chmod: cannot change mode of '%s'\n", argv[i]);
            ret = 1;
        }
    }
    return ret;
}
