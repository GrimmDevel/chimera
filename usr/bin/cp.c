// cp - copy files and directories
#include <kernel/xiu_types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int copy_file(const char *src, const char *dst) {
    int sfd = open(src, O_RDONLY, 0);
    if (sfd < 0) {
        printf("cp: cannot open '%s'\n", src);
        return 1;
    }

    struct stat st;
    if (fstat(sfd, &st) < 0) {
        close(sfd);
        printf("cp: cannot stat '%s'\n", src);
        return 1;
    }

    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode ? st.st_mode : 0644);
    if (dfd < 0) {
        close(sfd);
        printf("cp: cannot create '%s'\n", dst);
        return 1;
    }

    char buf[4096];
    i64 n;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        if (write(dfd, buf, n) != n) {
            printf("cp: write error to '%s'\n", dst);
            close(sfd);
            close(dfd);
            return 1;
        }
    }

    close(sfd);
    close(dfd);
    return 0;
}

static int copy_dir_recursive(const char *src, const char *dst);

static int copy_dir_recursive(const char *src, const char *dst) {
    mkdir(dst, 0755);

    DIR *dir = opendir(src);
    if (!dir) {
        printf("cp: cannot open directory '%s'\n", src);
        return 1;
    }

    struct dirent *de;
    int err = 0;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        char sub_src[512];
        char sub_dst[512];
        snprintf(sub_src, sizeof(sub_src), "%s/%s", src, de->d_name);
        snprintf(sub_dst, sizeof(sub_dst), "%s/%s", dst, de->d_name);

        struct stat st;
        if (stat(sub_src, &st) == 0 && S_ISDIR(st.st_mode)) {
            err |= copy_dir_recursive(sub_src, sub_dst);
        } else {
            err |= copy_file(sub_src, sub_dst);
        }
    }
    closedir(dir);
    return err;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("usage: cp [-r] <source...> <destination>\n");
        return 1;
    }

    bool recursive = false;
    int arg_start = 1;
    if (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-R") == 0) {
        recursive = true;
        arg_start = 2;
    }

    if (argc - arg_start < 2) {
        printf("cp: missing destination\n");
        return 1;
    }

    const char *dst = argv[argc - 1];
    struct stat dst_st;
    bool dst_is_dir = (stat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode));

    int ret = 0;
    for (int i = arg_start; i < argc - 1; i++) {
        const char *src = argv[i];
        struct stat src_st;
        if (stat(src, &src_st) < 0) {
            printf("cp: '%s': no such file or directory\n", src);
            ret = 1;
            continue;
        }

        char target[512];
        if (dst_is_dir) {
            const char *base = strrchr(src, '/');
            base = base ? base + 1 : src;
            snprintf(target, sizeof(target), "%s/%s", dst, base);
        } else {
            strncpy(target, dst, sizeof(target) - 1);
            target[sizeof(target) - 1] = '\0';
        }

        if (S_ISDIR(src_st.st_mode)) {
            if (!recursive) {
                printf("cp: -r not specified; omitting directory '%s'\n", src);
                ret = 1;
            } else {
                ret |= copy_dir_recursive(src, target);
            }
        } else {
            ret |= copy_file(src, target);
        }
    }
    return ret;
}
