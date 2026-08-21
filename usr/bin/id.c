/*
 * XIU Operating System — BSD id(1) Utility
 * usr/bin/id.c
 *
 * Implements standard BSD id:
 * usage: id [user]
 *        id -G [-n] [user]
 *        id -g [-nr] [user]
 *        id -u [-nr] [user]
 *        id -p [user]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

int main(int argc, char *argv[]) {
    int Gflag = 0, gflag = 0, uflag = 0, nflag = 0, rflag = 0, pflag = 0;
    int ch;

    while ((ch = getopt(argc, argv, "Ggnrup")) != -1) {
        switch (ch) {
        case 'G': Gflag = 1; break;
        case 'g': gflag = 1; break;
        case 'u': uflag = 1; break;
        case 'n': nflag = 1; break;
        case 'r': rflag = 1; break;
        case 'p': pflag = 1; break;
        default:
            fprintf(stderr, "usage: id [user]\n       id -G [-n] [user]\n       id -g [-nr] [user]\n       id -u [-nr] [user]\n       id -p [user]\n");
            return 1;
        }
    }
    argc -= optind;
    argv += optind;

    uid_t uid, euid;
    gid_t gid, egid;
    gid_t groups[16];
    int ngroups = 0;
    struct passwd *pw = NULL;

    if (argc > 0) {
        pw = getpwnam(argv[0]);
        if (!pw) {
            fprintf(stderr, "id: %s: no such user\n", argv[0]);
            return 1;
        }
        uid = euid = pw->pw_uid;
        gid = egid = pw->pw_gid;
        groups[0] = gid;
        ngroups = 1;
        /* Look up groups for this user */
        setgrent();
        struct group *gr;
        while ((gr = getgrent()) != NULL && ngroups < 16) {
            if (gr->gr_gid == gid) continue;
            if (gr->gr_mem) {
                for (int i = 0; gr->gr_mem[i] != NULL; i++) {
                    if (strcmp(gr->gr_mem[i], pw->pw_name) == 0) {
                        groups[ngroups++] = gr->gr_gid;
                        break;
                    }
                }
            }
        }
        endgrent();
    } else {
        uid = getuid();
        euid = geteuid();
        gid = getgid();
        egid = getegid();
        ngroups = getgroups(16, groups);
        if (ngroups < 0) ngroups = 0;
        pw = getpwuid(uid);
    }

    /* -u option */
    if (uflag) {
        uid_t target = rflag ? uid : euid;
        if (nflag) {
            const char *name = user_from_uid(target, 0);
            printf("%s\n", name ? name : "");
        } else {
            printf("%u\n", (unsigned int)target);
        }
        return 0;
    }

    /* -g option */
    if (gflag) {
        gid_t target = rflag ? gid : egid;
        if (nflag) {
            const char *name = group_from_gid(target, 0);
            printf("%s\n", name ? name : "");
        } else {
            printf("%u\n", (unsigned int)target);
        }
        return 0;
    }

    /* -G option */
    if (Gflag) {
        for (int i = 0; i < ngroups; i++) {
            if (i > 0) fputc(' ', stdout);
            if (nflag) {
                const char *name = group_from_gid(groups[i], 0);
                printf("%s", name ? name : "");
            } else {
                printf("%u", (unsigned int)groups[i]);
            }
        }
        fputc('\n', stdout);
        return 0;
    }

    /* -p option (human readable) */
    if (pflag) {
        const char *uname = user_from_uid(uid, 0);
        printf("uid\t%s\n", uname ? uname : "");
        const char *gname = group_from_gid(gid, 0);
        printf("groups\t%s", gname ? gname : "");
        for (int i = 0; i < ngroups; i++) {
            if (groups[i] == gid) continue;
            const char *n = group_from_gid(groups[i], 0);
            if (n) printf(" %s", n);
        }
        fputc('\n', stdout);
        return 0;
    }

    /* Default BSD id output: uid=... gid=... groups=... */
    const char *uname = user_from_uid(uid, 0);
    printf("uid=%u(%s)", (unsigned int)uid, uname ? uname : "unknown");

    const char *gname = group_from_gid(gid, 0);
    printf(" gid=%u(%s)", (unsigned int)gid, gname ? gname : "unknown");

    if (euid != uid) {
        const char *euname = user_from_uid(euid, 0);
        printf(" euid=%u(%s)", (unsigned int)euid, euname ? euname : "unknown");
    }
    if (egid != gid) {
        const char *egname = group_from_gid(egid, 0);
        printf(" egid=%u(%s)", (unsigned int)egid, egname ? egname : "unknown");
    }

    if (ngroups > 0) {
        printf(" groups=");
        for (int i = 0; i < ngroups; i++) {
            if (i > 0) fputc(',', stdout);
            const char *gn = group_from_gid(groups[i], 0);
            printf("%u(%s)", (unsigned int)groups[i], gn ? gn : "unknown");
        }
    }
    fputc('\n', stdout);
    return 0;
}
