/* =============================================================================
 * Chimera Operating System — User Space C Library
 * usr/libsystem/gen/isatty.c
 * ============================================================================= */

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

int isatty(int fd) {
    struct termios t;
    if (tcgetattr(fd, &t) == 0) {
        return 1;
    }
    errno = ENOTTY;
    return 0;
}
