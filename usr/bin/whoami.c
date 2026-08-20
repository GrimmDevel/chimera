// whoami - print current user
#include <kernel/xiu_types.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    uid_t uid = getuid();
    if (uid == 0) {
        printf("root\n");
    } else {
        printf("user_%d\n", (int)uid);
    }
    return 0;
}
