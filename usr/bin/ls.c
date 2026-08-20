// ls - list directory contents
#include <kernel/xiu_types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

static void format_mode(mode_t mode, char *buf) {
    buf[0] = S_ISDIR(mode) ? 'd' : (S_ISCHR(mode) ? 'c' : (S_ISBLK(mode) ? 'b' : '-'));
    buf[1] = (mode & 0400) ? 'r' : '-';
    buf[2] = (mode & 0200) ? 'w' : '-';
    buf[3] = (mode & 0100) ? 'x' : '-';
    buf[4] = (mode & 0040) ? 'r' : '-';
    buf[5] = (mode & 0020) ? 'w' : '-';
    buf[6] = (mode & 0010) ? 'x' : '-';
    buf[7] = (mode & 0004) ? 'r' : '-';
    buf[8] = (mode & 0002) ? 'w' : '-';
    buf[9] = (mode & 0001) ? 'x' : '-';
    buf[10] = '\0';
}

static int list_dir(const char *path, bool opt_l, bool opt_a, bool opt_1) {
    DIR *dir = opendir(path);
    if (!dir) {
        // try stating as single file
        struct stat st;
        if (stat(path, &st) == 0) {
            if (opt_l) {
                char mode_str[11];
                format_mode(st.st_mode, mode_str);
                printf("%s %2u root root %8llu %s\n", mode_str, st.st_nlink, (long long)st.st_size, path);
            } else {
                printf("%s\n", path);
            }
            return 0;
        }
        printf("ls: cannot access '%s': No such file or directory\n", path);
        return 1;
    }

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (!opt_a && de->d_name[0] == '.') continue;

        if (opt_l) {
            char subpath[512];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, de->d_name);
            struct stat st;
            st.st_mode = 0;
            st.st_size = 0;
            st.st_nlink = 1;
            stat(subpath, &st);

            char mode_str[11];
            format_mode(st.st_mode, mode_str);
            printf("%s %2u root root %8llu %s\n", mode_str, st.st_nlink, (long long)st.st_size, de->d_name);
        } else if (opt_1) {
            printf("%s\n", de->d_name);
        } else {
            printf("%s  ", de->d_name);
        }
    }
    if (!opt_l && !opt_1) printf("\n");

    closedir(dir);
    return 0;
}

int main(int argc, char *argv[]) {
    bool opt_l = false;
    bool opt_a = false;
    bool opt_1 = false;
    int arg = 1;

    while (arg < argc && argv[arg][0] == '-' && argv[arg][1] != '\0') {
        const char *p = &argv[arg][1];
        while (*p) {
            if (*p == 'l') opt_l = true;
            else if (*p == 'a') opt_a = true;
            else if (*p == '1') opt_1 = true;
            p++;
        }
        arg++;
    }

    if (arg >= argc) {
        return list_dir(".", opt_l, opt_a, opt_1);
    }

    int ret = 0;
    bool multiple = (argc - arg > 1);
    for (int i = arg; i < argc; i++) {
        if (multiple) printf("%s:\n", argv[i]);
        ret |= list_dir(argv[i], opt_l, opt_a, opt_1);
        if (multiple && i < argc - 1) printf("\n");
    }
    return ret;
}
