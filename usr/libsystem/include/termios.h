#ifndef _TERMIOS_H
#define _TERMIOS_H

#include <sys/types.h>

typedef u32 tcflag_t;
typedef u8  cc_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[32];
};

#define ICANON    0x0002
#define ECHO      0x0008
#define ISIG      0x0001
#define IEXTEN    0x8000
#define OPOST     0x0001
#define CS8       0x0030
#define BRKINT    0x0002
#define ICRNL     0x0400
#define INPCK     0x0010
#define ISTRIP    0x0020
#define IXON      0x0400
#define VMIN      6
#define VTIME     5

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);

#endif
