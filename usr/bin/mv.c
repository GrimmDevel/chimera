// mv - move / rename files
#include <kernel/xiu_types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int rename(const char *oldpath, const char *newpath);

static int move_file(const char *src, const char *dst) {
    if (rename(src, dst) == 0) return 0;

    // fallback to copy + unlink if cross-fs
    int sfd = open(src, O_RDONLY, 0);
    if (sfd < 0) {
        printf("mv: cannot open '%s'\n", src);
        return 1;
    }

    struct stat st;
    fstat(sfd, &st);

    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode ? st.st_mode : 0644);
    if (dfd < 0) {
        close(sfd);
        printf("mv: cannot create '%s'\n", dst);
        return 1;
    }

    char buf[4096];
    i64 n;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        write(dfd, buf, n);
    }
    close(sfd);
    close(dfd);

    unlink(src);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("usage: mv <source...> <destination>\n");
        return 1;
    }

    const char *dst = argv[argc - 1];
    struct stat dst_st;
    bool dst_is_dir = (stat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode));

    int ret = 0;
    for (int i = 1; i < argc - 1; i++) {
        const char *src = argv[i];
        char target[512];
        if (dst_is_dir) {
            const char *base = strrchr(src, '/');
            base = base ? base + 1 : src;
            snprintf(target, sizeof(target), "%s/%s", dst, base);
        } else {
            strncpy(target, dst, sizeof(target) - 1);
            target[sizeof(target) - 1] = '\0';
        }

        ret |= move_file(src, target);
    }
    return ret;
}
