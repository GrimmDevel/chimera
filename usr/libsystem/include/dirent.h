#ifndef _DIRENT_H
#define _DIRENT_H

#include <kernel/xiu_types.h>

#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12

struct dirent {
    u64 d_ino;
    u64 d_off;
    u16 d_reclen;
    u8  d_type;
    char d_name[256];
};

#define dirent64 dirent
#define readdir64 readdir

typedef struct {
    int fd;
    struct dirent entry;
} DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

#endif
