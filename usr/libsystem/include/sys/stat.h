#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

#define S_IFMT   0170000
#define S_IFIFO  0010000
#define S_IFCHR  0020000
#define S_IFDIR  0040000
#define S_IFBLK  0060000
#define S_IFREG  0100000
#define S_IFLNK  0120000
#define S_IFSOCK 0140000

#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100

#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010

#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000

struct stat {
    u32 st_dev;
    u32 st_ino;
    u32 st_mode;
    u32 st_nlink;
    u32 st_uid;
    u32 st_gid;
    u32 st_rdev;
    u64 st_size;
    u64 st_atime;
    u64 st_mtime;
    u64 st_ctime;
    u32 st_blksize;
    u32 st_blocks;
};

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int mkdir(const char *path, mode_t mode);

struct stat64 {
    u32 st_dev;
    u32 st_ino;
    u32 st_mode;
    u32 st_nlink;
    u32 st_uid;
    u32 st_gid;
    u32 st_rdev;
    u64 st_size;
    u64 st_atime;
    u64 st_mtime;
    u64 st_ctime;
    u32 st_blksize;
    u32 st_blocks;
};
int stat64(const char *path, struct stat64 *buf);
int lstat64(const char *path, struct stat64 *buf);
int fstat64(int fd, struct stat64 *buf);

#endif
