/* =============================================================================
 * XIU Operating System — launchd (PID 1)
 * usr/bin/launchd.c
 *
 * Real userland init:
 *   - spawns and supervises the console shell (/bin/zsh)
 *   - reaps orphaned children reparented to PID 1 (keeps the process pool
 *     from filling with zombies)
 *   - respawns the shell whenever it exits
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    printf("launchd: PID 1 online\n");

    for (;;) {
        pid_t pid = fork();
        if (pid == 0) {
            // child: become the console shell
            char *argv[] = {"zsh", NULL};
            char *envp[] = {"TERM=xterm-256color",
                            "PATH=/bin:/usr/bin:/sbin:/usr/local/bin",
                            "HOME=/Users/root", "USER=root", "PWD=/", NULL};
            execve("/bin/zsh", argv, envp);
            _exit(127); // exec failed
        }
        if (pid < 0) {
            // fork failed (process pool exhausted): back off and retry
            sleep(1);
            continue;
        }

        // reap every child; respawn the console shell when it exits.
        // orphans reparented to PID 1 by the kernel are harvested here too.
        int status = 0;
        pid_t r;
        while ((r = waitpid(-1, &status, 0)) > 0) {
            if (r == pid) break;
        }
        if (r < 0) {
            sleep(1);
            continue;
        }

        printf("launchd: console shell exited (status=%d), respawning\n",
               status);
    }
    return 0;
}
