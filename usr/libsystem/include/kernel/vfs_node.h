// vfs node (vnode) and mount structures
#pragma once
#ifndef CHIMERA_VFS_NODE_H
#define CHIMERA_VFS_NODE_H

#include <kernel/chimera_types.h>
#include <kernel/spinlock.h>

struct vnode;
struct vnode_ops;
struct mount;
struct mount_ops;
struct vfs_context;
struct uio;
struct stat;
struct dirent;

typedef u16 vtype_t;

#define VNON     ((vtype_t)0)
#define VREG     ((vtype_t)1)
#define VDIR     ((vtype_t)2)
#define VBLK     ((vtype_t)3)
#define VCHR     ((vtype_t)4)
#define VLNK     ((vtype_t)5)
#define VSOCK    ((vtype_t)6)
#define VFIFO    ((vtype_t)7)
#define VBAD     ((vtype_t)8)
#define VSTR     ((vtype_t)9)
#define VCPLX    ((vtype_t)10)

typedef u32 vflags_t;

#define VN_ROOT         (1u <<  0)
#define VN_MOUNT        (1u <<  1)
#define VN_DIRTY        (1u <<  2)
#define VN_LOCKED       (1u <<  3)
#define VN_WANT         (1u <<  4)
#define VN_INACTIVE     (1u <<  5)
#define VN_SYSTEM       (1u <<  6)
#define VN_NOEXEC       (1u <<  7)
#define VN_NOCACHE      (1u <<  8)
#define VN_APPEND       (1u <<  9)
#define VN_IMMUTABLE    (1u << 10)
#define VN_SYMLINK_SAFE (1u << 11)
#define VN_BUNDLEPATH   (1u << 12)

typedef struct vattr {
    u64         va_ino;
    vtype_t     va_type;
    u16         va_mode;
    u32         va_nlink;
    chimera_uid_t   va_uid;
    chimera_gid_t   va_gid;
    u32         va_rdev;
    u64         va_size;
    u64         va_blocks;
    u32         va_blksize;
    u64         va_atime_ns;
    u64         va_mtime_ns;
    u64         va_ctime_ns;
    u64         va_birthtime_ns;
    u32         va_flags;
    u32         va_gen;
} vattr_t;

typedef struct vfs_context {
    chimera_uid_t           vc_uid;
    chimera_gid_t           vc_gid;
    chimera_pid_t           vc_pid;
    struct chimera_thread  *vc_thread;
} vfs_context_t;

typedef struct vnode_ops {
    const char *vop_name;

    chimera_error_t (*vop_open)    (struct vnode *vp, int flags,
                                 vfs_context_t *ctx);
    chimera_error_t (*vop_close)   (struct vnode *vp, int flags,
                                 vfs_context_t *ctx);
    chimera_error_t (*vop_reclaim) (struct vnode *vp, vfs_context_t *ctx);
    chimera_error_t (*vop_inactive)(struct vnode *vp, vfs_context_t *ctx);

    chimera_error_t (*vop_getattr) (struct vnode *vp, vattr_t *attr,
                                 vfs_context_t *ctx);
    chimera_error_t (*vop_setattr) (struct vnode *vp, const vattr_t *attr,
                                 vfs_context_t *ctx);

    chimera_error_t (*vop_read)    (struct vnode *vp, struct uio *uio,
                                 int ioflags, vfs_context_t *ctx);
    chimera_error_t (*vop_write)   (struct vnode *vp, struct uio *uio,
                                 int ioflags, vfs_context_t *ctx);
    chimera_error_t (*vop_ioctl)   (struct vnode *vp, u64 cmd,
                                 chimera_vaddr_t arg, vfs_context_t *ctx);
    chimera_error_t (*vop_mmap)    (struct vnode *vp, chimera_offset_t offset,
                                 chimera_size_t size, int prot,
                                 vfs_context_t *ctx);

    chimera_error_t (*vop_lookup)  (struct vnode *dvp, struct vnode **vpp,
                                 const char *name, vfs_context_t *ctx);
    chimera_error_t (*vop_mkdir)   (struct vnode *dvp, struct vnode **vpp,
                                 const char *name, vattr_t *attr,
                                 vfs_context_t *ctx);
    chimera_error_t (*vop_rmdir)   (struct vnode *dvp, struct vnode *vp,
                                 const char *name, vfs_context_t *ctx);
    chimera_error_t (*vop_create)  (struct vnode *dvp, struct vnode **vpp,
                                 const char *name, vattr_t *attr,
                                 vfs_context_t *ctx);
    chimera_error_t (*vop_remove)  (struct vnode *dvp, struct vnode *vp,
                                 const char *name, vfs_context_t *ctx);
    chimera_error_t (*vop_rename)  (struct vnode *fdvp, struct vnode *fvp,
                                 const char *fname,
                                 struct vnode *tdvp, struct vnode *tvp,
                                 const char *tname,
                                 vfs_context_t *ctx);
    chimera_error_t (*vop_readdir) (struct vnode *dvp, struct uio *uio,
                                 vfs_context_t *ctx, int *eofflag);
    chimera_error_t (*vop_symlink) (struct vnode *dvp, struct vnode **vpp,
                                 const char *name, const char *target,
                                 vattr_t *attr, vfs_context_t *ctx);
    chimera_error_t (*vop_readlink)(struct vnode *vp, struct uio *uio,
                                 vfs_context_t *ctx);

    chimera_error_t (*vop_access)  (struct vnode *vp, int mode,
                                 vfs_context_t *ctx);

    chimera_error_t (*vop_pageout) (struct vnode *vp, chimera_vaddr_t page_va,
                                 chimera_size_t size, vfs_context_t *ctx);
    chimera_error_t (*vop_pagein)  (struct vnode *vp, chimera_vaddr_t page_va,
                                 chimera_offset_t offset, chimera_size_t size,
                                 vfs_context_t *ctx);
} vnode_ops_t;

typedef struct CHIMERA_ALIGNED(64) vnode {
    u64             v_signature;
    vtype_t         v_type;
    vflags_t        v_flags;
    atomic_uint     v_iocount;
    atomic_uint     v_usecount;

    spinlock_t      v_lock;

    struct mount   *v_mount;
    vnode_ops_t    *v_op;
    void           *v_data;

    char            v_name[256];
    struct vnode   *v_parent;

    struct vnode   *v_mountedhere;

    void           *v_ubc;

    struct vnode   *v_lru_next;
    struct vnode   *v_lru_prev;

    vattr_t         v_attr;
    bool            v_attr_valid;
} vnode_t;

#define CHIMERA_VNODE_MAGIC  UINT64_C(0x584955564E4F4445)

typedef u32 mnt_flags_t;
#define MNT_RDONLY      (1u << 0)
#define MNT_NOEXEC      (1u << 1)
#define MNT_NOSUID      (1u << 2)
#define MNT_NODEV       (1u << 3)
#define MNT_SYNCHRONOUS (1u << 4)
#define MNT_AUTOMOUNTED (1u << 5)
#define MNT_LOCAL       (1u << 6)
#define MNT_ROOTFS      (1u << 7)

typedef struct mount_ops {
    const char  *mop_name;
    chimera_error_t (*mop_mount)  (struct mount *mp, vnode_t *devvp,
                                const char *opts, vfs_context_t *ctx);
    chimera_error_t (*mop_unmount)(struct mount *mp, int flags,
                                vfs_context_t *ctx);
    chimera_error_t (*mop_root)   (struct mount *mp, vnode_t **rootvp,
                                vfs_context_t *ctx);
    chimera_error_t (*mop_sync)   (struct mount *mp, int waitfor,
                                vfs_context_t *ctx);
    chimera_error_t (*mop_statfs) (struct mount *mp, void *statbuf,
                                vfs_context_t *ctx);
} mount_ops_t;

typedef struct CHIMERA_ALIGNED(64) mount {
    u64             mnt_signature;
    spinlock_t      mnt_lock;
    mnt_flags_t     mnt_flags;

    vnode_t        *mnt_vnodecovered;
    vnode_t        *mnt_rootvp;

    mount_ops_t    *mnt_op;
    void           *mnt_data;

    char            mnt_vfstype[32];
    char            mnt_mountpath[1024];
    char            mnt_device[256];

    u64             mnt_blocks_total;
    u64             mnt_blocks_free;
    u32             mnt_files_total;
    u32             mnt_bsize;

    struct mount   *mnt_next;
} mount_t;

#define CHIMERA_MOUNT_MAGIC  UINT64_C(0x5849554D4E54504F)

extern vnode_t *vfs_root_vnode;

chimera_error_t vfs_init(void);

chimera_error_t vfs_mount(const char *fstype, const char *device,
                       const char *mountpath, mnt_flags_t flags,
                       vfs_context_t *ctx);
chimera_error_t vfs_unmount(const char *mountpath, int flags,
                         vfs_context_t *ctx);

chimera_error_t vnode_get(vnode_t *vp);
void        vnode_put(vnode_t *vp);
chimera_error_t vnode_ref(vnode_t *vp);
void        vnode_rele(vnode_t *vp);
chimera_error_t vnode_alloc(mount_t *mp, vtype_t type, vnode_ops_t *ops,
                         void *fsdata, vnode_t **vp_out);

chimera_error_t vfs_lookup_path(const char *path, vnode_t **vp_out,
                              vfs_context_t *ctx);

chimera_error_t vfs_register_filesystem(mount_ops_t *ops);

CHIMERA_ALWAYS_INLINE chimera_error_t
VOP_LOOKUP(vnode_t *dvp, vnode_t **vpp, const char *name, vfs_context_t *ctx) {
    if (!dvp->v_op->vop_lookup) return CHIMERA_ERR_NOTSUP;
    return dvp->v_op->vop_lookup(dvp, vpp, name, ctx);
}

CHIMERA_ALWAYS_INLINE chimera_error_t
VOP_GETATTR(vnode_t *vp, vattr_t *attr, vfs_context_t *ctx) {
    if (!vp->v_op->vop_getattr) return CHIMERA_ERR_NOTSUP;
    return vp->v_op->vop_getattr(vp, attr, ctx);
}

CHIMERA_ALWAYS_INLINE chimera_error_t
VOP_OPEN(vnode_t *vp, int flags, vfs_context_t *ctx) {
    if (!vp->v_op->vop_open) return CHIMERA_SUCCESS;
    return vp->v_op->vop_open(vp, flags, ctx);
}

CHIMERA_ALWAYS_INLINE chimera_error_t
VOP_RECLAIM(vnode_t *vp, vfs_context_t *ctx) {
    if (!vp->v_op->vop_reclaim) return CHIMERA_SUCCESS;
    return vp->v_op->vop_reclaim(vp, ctx);
}

#endif
