// stat - display file status
#include <kernel/xiu_types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <time.h>

static void print_file_type(mode_t mode) {
    if (S_ISREG(mode)) printf("regular file");
    else if (S_ISDIR(mode)) printf("directory");
    else if (S_ISCHR(mode)) printf("character device");
    else if (S_ISBLK(mode)) printf("block device");
    else if (S_ISFIFO(mode)) printf("FIFO");
    else if (S_ISLNK(mode)) printf("symbolic link");
    else if (S_ISSOCK(mode)) printf("socket");
    else printf("unknown");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: stat <file...>\n");
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) < 0) {
            printf("stat: cannot stat '%s'\n", argv[i]);
            ret = 1;
            continue;
        }

        printf("  File: %s\n", argv[i]);
        printf("  Size: %-10llu Blocks: %-5u IO Block: %-5u ",
               (long long)st.st_size, st.st_blocks, st.st_blksize ? st.st_blksize : 4096);
        print_file_type(st.st_mode);
        printf("\n");
        printf("Device: %uh/%ud   Inode: %-8u Links: %u\n",
               st.st_dev, st.st_dev, st.st_ino, st.st_nlink);
        printf("Access: (0%03o/", st.st_mode & 07777);
        printf("%c%c%c%c%c%c%c%c%c%c)  Uid: (%4u/    user)   Gid: (%4u/   group)\n",
               S_ISDIR(st.st_mode) ? 'd' : (S_ISCHR(st.st_mode) ? 'c' : '-'),
               (st.st_mode & 0400) ? 'r' : '-',
               (st.st_mode & 0200) ? 'w' : '-',
               (st.st_mode & 0100) ? 'x' : '-',
               (st.st_mode & 0040) ? 'r' : '-',
               (st.st_mode & 0020) ? 'w' : '-',
               (st.st_mode & 0010) ? 'x' : '-',
               (st.st_mode & 0004) ? 'r' : '-',
               (st.st_mode & 0002) ? 'w' : '-',
               (st.st_mode & 0001) ? 'x' : '-',
               st.st_uid, st.st_gid);
    }
    return ret;
}
