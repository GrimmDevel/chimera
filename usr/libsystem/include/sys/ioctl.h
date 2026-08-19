#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include <sys/types.h>

struct winsize {
    u16 ws_row;
    u16 ws_col;
    u16 ws_xpixel;
    u16 ws_ypixel;
};

#define TIOCGWINSZ 0x5413

int ioctl(int fd, u64 request, ...);

#endif
