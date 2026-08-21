#ifndef _SYS_CONSIO_H_
#define _SYS_CONSIO_H_

#include <sys/types.h>

#define VT_ACTIVATE   0x5606
#define VT_WAITACTIVE 0x5607
#define VT_GETACTIVE  0x5608
#define VT_OPENQRY    0x5609
#define VT_SETMODE    0x560a
#define VT_GETMODE    0x560b
#define VT_RELDISP    0x560c

#define VT_AUTO       0
#define VT_PROCESS    1
#define VT_ACKACQ     2

typedef struct vt_mode {
    char mode;
    char waitv;
    short relsig;
    short acqsig;
    short frsig;
} vtmode_t;

static inline int tcsetsid(int fd, pid_t pid) { (void)fd; (void)pid; return 0; }

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#endif
