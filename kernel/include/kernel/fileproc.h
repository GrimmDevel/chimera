// file descriptor and file procedure (fileproc)
#pragma once
#ifndef CHIMERA_FILEPROC_H
#define CHIMERA_FILEPROC_H

#include <kernel/chimera_types.h>
#include <kernel/spinlock.h>

struct vnode;
struct vfs_context;
struct socket;
struct chimera_proc;

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

typedef struct chimera_fileproc {
    u64              fp_signature;
    u32              fp_flags;
    u32              fp_type;
    _Atomic(u32)     fp_refcount;

    union {
        struct vnode    *fp_vnode;
        struct socket   *fp_socket;
        void            *fp_data;
    };
    chimera_offset_t     fp_offset;

    spinlock_t       fp_lock;
} chimera_fileproc_t;

#define CHIMERA_FILEPROC_MAGIC  0x4644455343522121ULL

// Standard fcntl commands (Darwin/XNU compatible)
#define F_DUPFD         0
#define F_GETFD         1
#define F_SETFD         2
#define F_GETFL         3
#define F_SETFL         4
#define F_GETOWN        5
#define F_SETOWN        6
#define F_GETLK         7
#define F_SETLK         8
#define F_SETLKW        9
#define F_DUPFD_CLOEXEC 67

// Standard FD flags
#define FD_CLOEXEC      1

// Standard open/status flags (Darwin/XNU compatible)
#define O_RDONLY        0x0000
#define O_WRONLY        0x0001
#define O_RDWR          0x0002
#define O_ACCMODE       0x0003
#define O_NONBLOCK      0x0004
#define O_APPEND        0x0008
#define O_CREAT         0x0200
#define O_TRUNC         0x0400
#define O_EXCL          0x0800
#define O_NOCTTY        0x20000
#define O_CLOEXEC       0x1000000

chimera_fileproc_t *fp_alloc(struct vnode *vp, u32 flags);
chimera_fileproc_t *fp_alloc_socket(struct socket *so, u32 flags);
void fp_release(chimera_fileproc_t *fp);

static inline void fp_retain(chimera_fileproc_t *fp) {
    atomic_fetch_add_explicit(&fp->fp_refcount, 1, memory_order_relaxed);
}

int  proc_fd_install(struct chimera_proc *p, chimera_fileproc_t *fp);
int  proc_fd_alloc_from(struct chimera_proc *p, chimera_fileproc_t *fp, int min_fd);
chimera_fileproc_t *proc_fd_lookup(struct chimera_proc *p, int fd);
u8   proc_fd_get_flags(struct chimera_proc *p, int fd);
void proc_fd_set_flags(struct chimera_proc *p, int fd, u8 flags);
chimera_error_t proc_fd_close(struct chimera_proc *p, int fd);
void proc_fd_close_cloexec(struct chimera_proc *p);
i16  fileproc_poll(chimera_fileproc_t *fp, i16 events);

#endif

