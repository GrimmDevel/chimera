// whoami - print current user
#include <kernel/chimera_types.h>
#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    uid_t uid = geteuid();
    struct passwd *pw = getpwuid(uid);
    if (pw && pw->pw_name) {
        printf("%s\n", pw->pw_name);
        return 0;
    }
    const char *u = getenv("USER");
    if (u && u[0]) {
        printf("%s\n", u);
        return 0;
    }
    if (uid == 0) {
        printf("root\n");
    } else {
        printf("user_%d\n", (int)uid);
    }
    return 0;
}
