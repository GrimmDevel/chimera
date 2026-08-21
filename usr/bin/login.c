/*
 * XIU Operating System — BSD login(1) Utility
 * usr/bin/login.c
 *
 * Implements standard BSD login:
 * usage: login [-fp] [-h hostname] [username]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>

static void get_password(const char *prompt, char *buf, size_t buflen) {
    fputs(prompt, stdout);
    fflush(stdout);

    struct termios orig_t, no_echo_t;
    int is_tty = isatty(STDIN_FILENO);
    if (is_tty) {
        tcgetattr(STDIN_FILENO, &orig_t);
        no_echo_t = orig_t;
        no_echo_t.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
        tcsetattr(STDIN_FILENO, TCSANOW, &no_echo_t);
    }

    buf[0] = '\0';
    if (fgets(buf, (int)buflen, stdin) != NULL) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = '\0';
    }

    if (is_tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_t);
        fputc('\n', stdout);
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {
    int fflag = 0;
    int pflag = 0;
    const char *username = NULL;
    int ch;

    while ((ch = getopt(argc, argv, "fh:p")) != -1) {
        switch (ch) {
        case 'f':
            fflag = 1;
            break;
        case 'p':
            pflag = 1;
            break;
        case 'h':
            /* Hostname - ignored for now */
            break;
        default:
            fprintf(stderr, "usage: login [-fp] [-h hostname] [username]\n");
            return 1;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc > 0) {
        username = argv[0];
    }

    /* Only root can use -f */
    if (fflag && getuid() != 0) {
        fflag = 0;
    }

    char userbuf[64];
    for (;;) {
        if (!username || username[0] == '\0') {
            fputs("login: ", stdout);
            fflush(stdout);
            if (fgets(userbuf, sizeof(userbuf), stdin) == NULL) {
                return 0;
            }
            size_t len = strlen(userbuf);
            if (len > 0 && userbuf[len - 1] == '\n')
                userbuf[len - 1] = '\0';
            if (userbuf[0] == '\0')
                continue;
            username = userbuf;
        }

        struct passwd *pw = getpwnam(username);
        if (!pw) {
            /* Dummy crypt to avoid timing attack */
            char passbuf[128];
            get_password("Password: ", passbuf, sizeof(passbuf));
            crypt(passbuf, "$6$xxxx$");
            explicit_bzero(passbuf, sizeof(passbuf));
            puts("Login incorrect");
            username = NULL;
            continue;
        }

        int auth_ok = 0;
        if (fflag) {
            auth_ok = 1;
        } else if (!pw->pw_passwd || pw->pw_passwd[0] == '\0') {
            /* Empty password */
            auth_ok = 1;
        } else if (pw->pw_passwd[0] == '*') {
            /* Locked account */
            auth_ok = 0;
        } else {
            char passbuf[128];
            get_password("Password: ", passbuf, sizeof(passbuf));
            char *hash = crypt(passbuf, pw->pw_passwd);
            if (hash && strcmp(hash, pw->pw_passwd) == 0) {
                auth_ok = 1;
            }
            explicit_bzero(passbuf, sizeof(passbuf));
        }

        if (!auth_ok) {
            puts("Login incorrect");
            username = NULL;
            continue;
        }

        /* Authentication successful: initialize session */
        setlogin(pw->pw_name);
        initgroups(pw->pw_name, (int)pw->pw_gid);
        setgid(pw->pw_gid);
        setuid(pw->pw_uid);

        /* Set environment */
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("HOME", pw->pw_dir ? pw->pw_dir : "/", 1);
        setenv("SHELL", pw->pw_shell ? pw->pw_shell : "/bin/zsh", 1);
        if (!pflag) {
            setenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin", 1);
        }

        /* Change to home directory */
        if (chdir(pw->pw_dir ? pw->pw_dir : "/") < 0) {
            printf("No directory %s!\nLogging in with home = \"/\".\n", pw->pw_dir);
            chdir("/");
            setenv("HOME", "/", 1);
        }

        /* Execute shell as login shell (-shell) */
        const char *shell = pw->pw_shell && pw->pw_shell[0] ? pw->pw_shell : "/bin/zsh";
        const char *slash = strrchr(shell, '/');
        const char *shname = slash ? slash + 1 : shell;
        char arg0[64];
        snprintf(arg0, sizeof(arg0), "-%s", shname);

        execl(shell, arg0, (char *)NULL);
        fprintf(stderr, "Cannot execute %s: %s\n", shell, strerror(errno));
        return 1;
    }

    return 0;
}
