/* =============================================================================
 * XIU Operating System — Remove File/Directory Utility (rm)
 * usr/bin/rm.c
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

extern int unlink(const char *path);
extern int rmdir(const char *path);

static void print_usage(const char *prog) {
    printf("usage: %s [-f] [-r] [-v] file ...\n", prog);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    int force = 0;
    int recursive = 0;
    int verbose = 0;
    int target_idx = 1;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (const char *p = argv[i] + 1; *p; p++) {
                if (*p == 'f') force = 1;
                else if (*p == 'r' || *p == 'R') recursive = 1;
                else if (*p == 'v') verbose = 1;
                else if (*p == 'h') {
                    print_usage(argv[0]);
                    return 0;
                }
            }
            target_idx = i + 1;
        } else {
            break;
        }
    }

    if (target_idx >= argc) {
        if (!force) print_usage(argv[0]);
        return force ? 0 : 1;
    }

    int ret = 0;
    for (int i = target_idx; i < argc; i++) {
        const char *path = argv[i];
        if (path[0] == '-') continue;

        int res = unlink(path);
        if (res != 0 && recursive) {
            res = rmdir(path);
        }

        if (res == 0) {
            if (verbose) printf("removed '%s'\n", path);
        } else {
            if (!force) {
                printf("rm: cannot remove '%s'\n", path);
                ret = 1;
            }
        }
    }

    return ret;
}
