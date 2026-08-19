/* =============================================================================
 * XIU Operating System — Apple XNU VFS Mount & File System Info
 * kernel/include/kernel/mount_xnu.h
 * Derived from XNU bsd/sys/mount.h
 * ============================================================================= */

#ifndef XIU_MOUNT_XNU_H
#define XIU_MOUNT_XNU_H

#include <kernel/xiu_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MFSNAMELEN  15
#define MNAMELEN    90

struct statfs {
    u32 f_bsize;            // fundamental file system block size
    u32 f_iosize;           // optimal transfer block size
    u64 f_blocks;           // total data blocks in file system
    u64 f_bfree;            // free blocks in file system
    u64 f_bavail;           // free blocks available to unprivileged
    u64 f_files;            // total file nodes in file system
    u64 f_ffree;            // free file nodes in file system
    u32 f_fsid[2];          // file system ID
    u32 f_owner;            // user ID that mounted file system
    u32 f_type;             // type of file system
    u32 f_flags;            // copy of mount exported flags
    char f_fstypename[MFSNAMELEN]; // file system type name
    char f_mntonname[MNAMELEN];   // directory mounted upon
    char f_mntfromname[MNAMELEN]; // mounted file system name
};

#define MNT_RDONLY      0x00000001
#define MNT_SYNCHRONOUS 0x00000002
#define MNT_NOEXEC      0x00000004
#define MNT_NOSUID      0x00000008
#define MNT_LOCAL       0x00001000

#ifdef __cplusplus
}
#endif

#endif /* XIU_MOUNT_XNU_H */
