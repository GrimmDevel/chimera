// env - print or run in environment
#include <kernel/chimera_types.h>
#include <stdio.h>
#include <unistd.h>

extern char **environ;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        if (environ) {
            for (char **env = environ; *env; env++) {
                printf("%s\n", *env);
            }
        }
        return 0;
    }

    // execute command
    execvp(argv[1], &argv[1]);
    printf("env: %s: command not found\n", argv[1]);
    return 127;
}
