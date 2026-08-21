/*
 * XIU Operating System — BSD su(1) Utility
 * usr/bin/su.c
 *
 * Implements standard BSD su (switch user):
 * usage: su [-] [-l] [-m] [username]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <termios.h>
#include <errno.h>

static void get_password(const char *prompt, char *buf, size_t buflen) {
    fputs(prompt, stderr);
    fflush(stderr);

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
        fputc('\n', stderr);
    }
}

int main(int argc, char *argv[]) {
    int login_shell = 0;
    int preserve_env = 0;
    const char *username = "root";

    int opt_start = 1;
    while (opt_start < argc) {
        if (strcmp(argv[opt_start], "-") == 0 || strcmp(argv[opt_start], "-l") == 0) {
            login_shell = 1;
            opt_start++;
        } else if (strcmp(argv[opt_start], "-m") == 0) {
            preserve_env = 1;
            opt_start++;
        } else {
            break;
        }
    }

    if (opt_start < argc) {
        username = argv[opt_start];
    }

    struct passwd *pw = getpwnam(username);
    if (!pw) {
        fprintf(stderr, "su: unknown login: %s\n", username);
        return 1;
    }

    uid_t my_uid = getuid();

    /* If not root, check password */
    if (my_uid != 0) {
        if (pw->pw_passwd && pw->pw_passwd[0] != '\0' && pw->pw_passwd[0] != '*') {
            char passbuf[128];
            get_password("Password: ", passbuf, sizeof(passbuf));
            char *hash = crypt(passbuf, pw->pw_passwd);
            int ok = (hash && strcmp(hash, pw->pw_passwd) == 0);
            explicit_bzero(passbuf, sizeof(passbuf));
            if (!ok) {
                fprintf(stderr, "su: Sorry\n");
                return 1;
            }
        }
    }

    /* Switch credentials */
    initgroups(pw->pw_name, (int)pw->pw_gid);
    setgid(pw->pw_gid);
    setuid(pw->pw_uid);

    /* Environment setup */
    if (login_shell) {
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("HOME", pw->pw_dir ? pw->pw_dir : "/", 1);
        setenv("SHELL", pw->pw_shell ? pw->pw_shell : "/bin/zsh", 1);
        setenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin", 1);
        if (chdir(pw->pw_dir ? pw->pw_dir : "/") < 0) {
            chdir("/");
        }
    } else if (!preserve_env) {
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("HOME", pw->pw_dir ? pw->pw_dir : "/", 1);
        setenv("SHELL", pw->pw_shell ? pw->pw_shell : "/bin/zsh", 1);
    }

    const char *shell = pw->pw_shell && pw->pw_shell[0] ? pw->pw_shell : "/bin/zsh";
    const char *slash = strrchr(shell, '/');
    const char *shname = slash ? slash + 1 : shell;
    char arg0[64];
    if (login_shell) {
        snprintf(arg0, sizeof(arg0), "-%s", shname);
    } else {
        snprintf(arg0, sizeof(arg0), "%s", shname);
    }

    execl(shell, arg0, (char *)NULL);
    fprintf(stderr, "su: cannot execute %s: %s\n", shell, strerror(errno));
    return 1;
}
