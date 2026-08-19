#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

#define O_RDONLY     0
#define O_WRONLY     1
#define O_RDWR       2
#define O_CREAT      64
#define O_EXCL       128
#define O_TRUNC      512
#define O_APPEND     1024
#define O_NONBLOCK   2048
#define O_DIRECTORY  65536

#define F_DUPFD      0
#define F_GETFD      1
#define F_SETFD      2
#define F_GETFL      3
#define F_SETFL      4

#define FD_CLOEXEC   1

int open(const char *path, int flags, ...);
int fcntl(int fd, int cmd, ...);
#define open64 open

#endif
