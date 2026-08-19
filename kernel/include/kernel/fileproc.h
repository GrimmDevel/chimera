// file descriptor and file procedure (fileproc)
#pragma once
#ifndef XIU_FILEPROC_H
#define XIU_FILEPROC_H

#include <kernel/xiu_types.h>
#include <kernel/spinlock.h>

struct vnode;
struct vfs_context;
struct socket;
struct xiu_proc;

#define FP_CLOEXEC      (1u << 0)
#define FP_NONBLOCK     (1u << 1)
#define FP_READABLE     (1u << 2)
#define FP_WRITABLE     (1u << 3)
#define FP_APPEND       (1u << 4)
#define FP_DEVICE       (1u << 5)
#define FP_WANTSREAD    (1u << 6)

#define DTYPE_VNODE     1
#define DTYPE_SOCKET    2
#define DTYPE_PIPE      3

typedef struct xiu_fileproc {
    u64              fp_signature;
    u32              fp_flags;
    u32              fp_type;
    _Atomic(u32)     fp_refcount;

    union {
        struct vnode    *fp_vnode;
        struct socket   *fp_socket;
        void            *fp_data;
    };
    xiu_offset_t     fp_offset;

    spinlock_t       fp_lock;
} xiu_fileproc_t;

#define XIU_FILEPROC_MAGIC  0x4644455343522121ULL

xiu_fileproc_t *fp_alloc(struct vnode *vp, u32 flags);
xiu_fileproc_t *fp_alloc_socket(struct socket *so, u32 flags);
void fp_release(xiu_fileproc_t *fp);

static inline void fp_retain(xiu_fileproc_t *fp) {
    atomic_fetch_add_explicit(&fp->fp_refcount, 1, memory_order_relaxed);
}

int  proc_fd_install(struct xiu_proc *p, xiu_fileproc_t *fp);
xiu_fileproc_t *proc_fd_lookup(struct xiu_proc *p, int fd);
xiu_error_t proc_fd_close(struct xiu_proc *p, int fd);

#endif
