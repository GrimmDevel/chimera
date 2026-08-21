/*
 * XIU Operating System — BSD User and Group Database Reader
 * usr/libsystem/pwd_grp.c
 *
 * Implements standard BSD POSIX interfaces:
 * getpwent, getpwnam, getpwuid, setpwent, endpwent,
 * getgrent, getgrnam, getgrgid, setgrent, endgrent,
 * user_from_uid, group_from_gid
 * Reading from /etc/master.passwd, /etc/passwd, and /etc/group.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <errno.h>

#ifndef LINE_MAX
#define LINE_MAX 2048
#endif

#define GRMEM_MAX 64

/* =========================================================================
 * User Database (/etc/master.passwd / /etc/passwd)
 * ========================================================================= */

static FILE *s_pwf = NULL;
static int s_pw_stayopen = 0;
static const char *s_pwfile = "/etc/master.passwd";

void setpwfile(const char *file) {
    s_pwfile = file;
    if (s_pwf != NULL) {
        endpwent();
    }
}

static int open_passwd(int reset) {
    if (s_pwf == NULL) {
        s_pwf = fopen(s_pwfile, "r");
        if (s_pwf == NULL && strcmp(s_pwfile, "/etc/passwd") != 0) {
            /* Fallback to standard /etc/passwd */
            s_pwf = fopen("/etc/passwd", "r");
        }
        if (s_pwf != NULL) {
            fcntl(fileno(s_pwf), F_SETFD, FD_CLOEXEC);
        }
        if (s_pwf == NULL)
            return 0;
    } else if (reset) {
        rewind(s_pwf);
    }
    return 1;
}

int setpassent(int stayopen) {
    if (!open_passwd(1))
        return 0;
    s_pw_stayopen = stayopen;
    return 1;
}

void setpwent(void) {
    setpassent(0);
}

void endpwent(void) {
    if (s_pwf != NULL) {
        fclose(s_pwf);
        s_pwf = NULL;
    }
    s_pw_stayopen = 0;
}

/*
 * Parse a password line:
 * master.passwd format (10 fields):
 *   name:password:uid:gid:class:change:expire:gecos:dir:shell
 * standard passwd format (7 fields):
 *   name:password:uid:gid:gecos:dir:shell
 */
int getpwent_r(struct passwd *pw, char *buf, size_t buflen, struct passwd **result) {
    if (!pw || !buf || buflen < 128 || !result) {
        if (result) *result = NULL;
        return EINVAL;
    }
    *result = NULL;

    if (!open_passwd(0))
        return ENOENT;

    char linebuf[LINE_MAX];
    while (fgets(linebuf, sizeof(linebuf), s_pwf) != NULL) {
        /* Skip comments and empty lines */
        char *line = linebuf;
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '#' || *line == '\n' || *line == '\0')
            continue;

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        if (len >= buflen)
            return ERANGE;
        memcpy(buf, line, len + 1);

        char *fields[11];
        int nfields = 0;
        char *p = buf;
        fields[nfields++] = p;

        while (*p && nfields < 11) {
            if (*p == ':') {
                *p = '\0';
                fields[nfields++] = p + 1;
            }
            p++;
        }

        if (nfields < 7)
            continue;

        memset(pw, 0, sizeof(*pw));
        pw->pw_name = fields[0];
        pw->pw_passwd = fields[1];
        pw->pw_uid = (uid_t)strtoul(fields[2], NULL, 10);
        pw->pw_gid = (gid_t)strtoul(fields[3], NULL, 10);

        if (nfields >= 10) {
            /* master.passwd format */
            pw->pw_class = fields[4];
            pw->pw_change = (time_t)strtoul(fields[5], NULL, 10);
            pw->pw_expire = (time_t)strtoul(fields[6], NULL, 10);
            pw->pw_gecos = fields[7];
            pw->pw_dir = fields[8];
            pw->pw_shell = fields[9];
        } else {
            /* standard passwd format */
            pw->pw_class = "";
            pw->pw_change = 0;
            pw->pw_expire = 0;
            pw->pw_gecos = fields[4];
            pw->pw_dir = fields[5];
            pw->pw_shell = fields[6];
        }
        pw->pw_fields = _PWF_FILES | _PWF_NAME | _PWF_PASSWD | _PWF_UID | _PWF_GID | _PWF_DIR | _PWF_SHELL;

        *result = pw;
        return 0;
    }

    return 0;
}

struct passwd *getpwent(void) {
    static struct passwd s_pw;
    static char s_buf[LINE_MAX];
    struct passwd *res = NULL;

    if (getpwent_r(&s_pw, s_buf, sizeof(s_buf), &res) == 0 && res != NULL)
        return res;
    return NULL;
}

int getpwnam_r(const char *name, struct passwd *pw, char *buf, size_t buflen, struct passwd **result) {
    if (!name || !pw || !buf || !result) {
        if (result) *result = NULL;
        return EINVAL;
    }
    *result = NULL;

    if (!open_passwd(1))
        return ENOENT;

    struct passwd cur_pw;
    struct passwd *res = NULL;
    int err = 0;

    while ((err = getpwent_r(&cur_pw, buf, buflen, &res)) == 0 && res != NULL) {
        if (strcmp(cur_pw.pw_name, name) == 0) {
            *pw = cur_pw;
            *result = pw;
            break;
        }
    }

    if (!s_pw_stayopen) {
        fclose(s_pwf);
        s_pwf = NULL;
    }
    return err;
}

struct passwd *getpwnam(const char *name) {
    static struct passwd s_pw;
    static char s_buf[LINE_MAX];
    struct passwd *res = NULL;

    if (getpwnam_r(name, &s_pw, s_buf, sizeof(s_buf), &res) == 0 && res != NULL)
        return res;
    return NULL;
}

int getpwuid_r(uid_t uid, struct passwd *pw, char *buf, size_t buflen, struct passwd **result) {
    if (!pw || !buf || !result) {
        if (result) *result = NULL;
        return EINVAL;
    }
    *result = NULL;

    if (!open_passwd(1))
        return ENOENT;

    struct passwd cur_pw;
    struct passwd *res = NULL;
    int err = 0;

    while ((err = getpwent_r(&cur_pw, buf, buflen, &res)) == 0 && res != NULL) {
        if (cur_pw.pw_uid == uid) {
            *pw = cur_pw;
            *result = pw;
            break;
        }
    }

    if (!s_pw_stayopen) {
        fclose(s_pwf);
        s_pwf = NULL;
    }
    return err;
}

struct passwd *getpwuid(uid_t uid) {
    static struct passwd s_pw;
    static char s_buf[LINE_MAX];
    struct passwd *res = NULL;

    if (getpwuid_r(uid, &s_pw, s_buf, sizeof(s_buf), &res) == 0 && res != NULL)
        return res;
    return NULL;
}

char *user_from_uid(uid_t uid, int nouser) {
    static char numbuf[32];
    struct passwd *pw = getpwuid(uid);
    if (pw && pw->pw_name)
        return pw->pw_name;
    if (nouser)
        return NULL;
    snprintf(numbuf, sizeof(numbuf), "%u", (unsigned int)uid);
    return numbuf;
}

int uid_from_user(const char *name, uid_t *uid) {
    if (!name || !uid) return -1;
    struct passwd *pw = getpwnam(name);
    if (pw) {
        *uid = pw->pw_uid;
        return 0;
    }
    char *ep = NULL;
    unsigned long val = strtoul(name, &ep, 10);
    if (ep && *ep == '\0') {
        *uid = (uid_t)val;
        return 0;
    }
    return -1;
}

/* =========================================================================
 * Group Database (/etc/group)
 * ========================================================================= */

static FILE *s_grf = NULL;
static int s_gr_stayopen = 0;
static const char *s_grfile = "/etc/group";

void setgrfile(const char *file) {
    s_grfile = file;
    if (s_grf != NULL) {
        endgrent();
    }
}

static int open_group(int reset) {
    if (s_grf == NULL) {
        s_grf = fopen(s_grfile, "r");
        if (s_grf != NULL) {
            fcntl(fileno(s_grf), F_SETFD, FD_CLOEXEC);
        }
        if (s_grf == NULL)
            return 0;
    } else if (reset) {
        rewind(s_grf);
    }
    return 1;
}

int setgroupent(int stayopen) {
    if (!open_group(1))
        return 0;
    s_gr_stayopen = stayopen;
    return 1;
}

void setgrent(void) {
    setgroupent(0);
}

void endgrent(void) {
    if (s_grf != NULL) {
        fclose(s_grf);
        s_grf = NULL;
    }
    s_gr_stayopen = 0;
}

int getgrent_r(struct group *grp, char *buf, size_t buflen, struct group **result) {
    if (!grp || !buf || buflen < 128 || !result) {
        if (result) *result = NULL;
        return EINVAL;
    }
    *result = NULL;

    if (!open_group(0))
        return ENOENT;

    char linebuf[LINE_MAX];
    while (fgets(linebuf, sizeof(linebuf), s_grf) != NULL) {
        char *line = linebuf;
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '#' || *line == '\n' || *line == '\0')
            continue;

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        if (len >= buflen)
            return ERANGE;
        memcpy(buf, line, len + 1);

        char *colon = strchr(buf, ':');
        if (!colon) continue;
        *colon++ = '\0';
        grp->gr_name = buf;

        char *cp = colon;
        colon = strchr(cp, ':');
        if (!colon) continue;
        *colon++ = '\0';
        grp->gr_passwd = cp;

        cp = colon;
        colon = strchr(cp, ':');
        if (!colon) continue;
        *colon++ = '\0';
        grp->gr_gid = (gid_t)strtoul(cp, NULL, 10);

        /* Members list */
        static char *s_members[GRMEM_MAX + 1];
        int mcnt = 0;
        char *last = NULL;
        char *mem = strtok_r(colon, ",", &last);
        while (mem != NULL && mcnt < GRMEM_MAX) {
            while (*mem == ' ' || *mem == '\t') mem++;
            s_members[mcnt++] = mem;
            mem = strtok_r(NULL, ",", &last);
        }
        s_members[mcnt] = NULL;
        grp->gr_mem = s_members;

        *result = grp;
        return 0;
    }

    return 0;
}

struct group *getgrent(void) {
    static struct group s_grp;
    static char s_buf[LINE_MAX];
    struct group *res = NULL;

    if (getgrent_r(&s_grp, s_buf, sizeof(s_buf), &res) == 0 && res != NULL)
        return res;
    return NULL;
}

int getgrnam_r(const char *name, struct group *grp, char *buf, size_t buflen, struct group **result) {
    if (!name || !grp || !buf || !result) {
        if (result) *result = NULL;
        return EINVAL;
    }
    *result = NULL;

    if (!open_group(1))
        return ENOENT;

    struct group cur_grp;
    struct group *res = NULL;
    int err = 0;

    while ((err = getgrent_r(&cur_grp, buf, buflen, &res)) == 0 && res != NULL) {
        if (strcmp(cur_grp.gr_name, name) == 0) {
            *grp = cur_grp;
            *result = grp;
            break;
        }
    }

    if (!s_gr_stayopen) {
        fclose(s_grf);
        s_grf = NULL;
    }
    return err;
}

struct group *getgrnam(const char *name) {
    static struct group s_grp;
    static char s_buf[LINE_MAX];
    struct group *res = NULL;

    if (getgrnam_r(name, &s_grp, s_buf, sizeof(s_buf), &res) == 0 && res != NULL)
        return res;
    return NULL;
}

int getgrgid_r(gid_t gid, struct group *grp, char *buf, size_t buflen, struct group **result) {
    if (!grp || !buf || !result) {
        if (result) *result = NULL;
        return EINVAL;
    }
    *result = NULL;

    if (!open_group(1))
        return ENOENT;

    struct group cur_grp;
    struct group *res = NULL;
    int err = 0;

    while ((err = getgrent_r(&cur_grp, buf, buflen, &res)) == 0 && res != NULL) {
        if (cur_grp.gr_gid == gid) {
            *grp = cur_grp;
            *result = grp;
            break;
        }
    }

    if (!s_gr_stayopen) {
        fclose(s_grf);
        s_grf = NULL;
    }
    return err;
}

struct group *getgrgid(gid_t gid) {
    static struct group s_grp;
    static char s_buf[LINE_MAX];
    struct group *res = NULL;

    if (getgrgid_r(gid, &s_grp, s_buf, sizeof(s_buf), &res) == 0 && res != NULL)
        return res;
    return NULL;
}

char *group_from_gid(gid_t gid, int nogroup) {
    static char numbuf[32];
    struct group *gr = getgrgid(gid);
    if (gr && gr->gr_name)
        return gr->gr_name;
    if (nogroup)
        return NULL;
    snprintf(numbuf, sizeof(numbuf), "%u", (unsigned int)gid);
    return numbuf;
}

int gid_from_group(const char *name, gid_t *gid) {
    if (!name || !gid) return -1;
    struct group *gr = getgrnam(name);
    if (gr) {
        *gid = gr->gr_gid;
        return 0;
    }
    char *ep = NULL;
    unsigned long val = strtoul(name, &ep, 10);
    if (ep && *ep == '\0') {
        *gid = (gid_t)val;
        return 0;
    }
    return -1;
}

int initgroups(const char *name, int basegid) {
    if (!name) return -1;
    gid_t groups[16];
    int ngroups = 0;
    groups[ngroups++] = (gid_t)basegid;

    setgrent();
    struct group *gr;
    while ((gr = getgrent()) != NULL && ngroups < 16) {
        if (gr->gr_gid == (gid_t)basegid) continue;
        if (gr->gr_mem) {
            for (int i = 0; gr->gr_mem[i] != NULL; i++) {
                if (strcmp(gr->gr_mem[i], name) == 0) {
                    groups[ngroups++] = gr->gr_gid;
                    break;
                }
            }
        }
    }
    endgrent();

    return setgroups(ngroups, groups);
}

int getgrouplist(const char *name, int basegid, int *groups, int *ngroups) {
    if (!name || !groups || !ngroups || *ngroups < 1) {
        if (ngroups) *ngroups = 1;
        return -1;
    }
    int max_groups = *ngroups;
    int count = 0;
    groups[count++] = basegid;

    setgrent();
    struct group *gr;
    while ((gr = getgrent()) != NULL) {
        if (gr->gr_gid == (gid_t)basegid) continue;
        if (gr->gr_mem) {
            for (int i = 0; gr->gr_mem[i] != NULL; i++) {
                if (strcmp(gr->gr_mem[i], name) == 0) {
                    if (count < max_groups) {
                        groups[count] = (int)gr->gr_gid;
                    }
                    count++;
                    break;
                }
            }
        }
    }
    endgrent();

    if (count > max_groups) {
        *ngroups = count;
        return -1;
    }
    *ngroups = count;
    return count;
}

