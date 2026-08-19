/* =============================================================================
 * XIU Operating System — File Descriptor Table Implementation
 * kernel/bsd/fileproc.c
 *
 * Implements per-process file descriptor allocation, lookup, and release.
 * Uses a static pool (Stage 2 — zone allocator integration is a future task).
 *
 * Thread-safety model:
 *   - All FDT mutations hold proc_t::p_fdlock (irqsave variant).
 *   - fp_refcount is managed via C11 atomics.
 *   - vnode operations are protected by their own vnode-level lock.
 * ============================================================================= */

#include <kernel/fileproc.h>
#include <kernel/proc.h>
#include <kernel/vfs_node.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>

#define FILEPROC_POOL_SIZE  512

static xiu_fileproc_t  s_fp_pool[FILEPROC_POOL_SIZE];
static u8              s_fp_used[FILEPROC_POOL_SIZE]; // 1 = allocated
static spinlock_t      s_fp_pool_lock = SPINLOCK_INIT;

/* ═══════════════════════════════════════════════════════════════════════════
 * fp_alloc — allocate a new fileproc backed by vp.
 *
 * Returns a fileproc with refcount=1, or NULL if the pool is exhausted.
 * The caller must call proc_fd_install() to attach it to a process.
 * ═══════════════════════════════════════════════════════════════════════════ */
xiu_fileproc_t *fp_alloc(vnode_t *vp, u32 flags) {
    XIU_ASSERT(vp != nullptr);

    irq_flags_t irq = spinlock_lock_irqsave(&s_fp_pool_lock);

    xiu_fileproc_t *fp = nullptr;
    for (usize i = 0; i < FILEPROC_POOL_SIZE; i++) {
        if (!s_fp_used[i]) {
            s_fp_used[i] = 1;
            fp = &s_fp_pool[i];
            break;
        }
    }

    spinlock_unlock_irqrestore(&s_fp_pool_lock, irq);

    if (!fp) {
        kprintf("[fileproc] ERROR: fp_alloc pool exhausted (%u entries)\n",
                FILEPROC_POOL_SIZE);
        return nullptr;
    }

    __builtin_memset(fp, 0, sizeof(*fp));
    fp->fp_signature = XIU_FILEPROC_MAGIC;
    fp->fp_flags     = flags | FP_READABLE;   // readable by default
    if (flags & FP_WRITABLE) fp->fp_flags |= FP_WRITABLE;
    if (vp->v_type == VCHR || vp->v_type == VBLK) fp->fp_flags |= FP_DEVICE;

    fp->fp_type      = DTYPE_VNODE;
    fp->fp_vnode     = vp;
    fp->fp_offset    = 0;
    atomic_store_explicit(&fp->fp_refcount, 1, memory_order_relaxed);
    spinlock_init(&fp->fp_lock);

    // bump the vnode reference count
    irq_flags_t virq = spinlock_lock_irqsave(&vp->v_lock);
    vp->v_iocount++;
    spinlock_unlock_irqrestore(&vp->v_lock, virq);

    return fp;
}

xiu_fileproc_t *fp_alloc_socket(struct socket *so, u32 flags) {
    if (!so) return nullptr;

    irq_flags_t irq = spinlock_lock_irqsave(&s_fp_pool_lock);
    xiu_fileproc_t *fp = nullptr;
    for (usize i = 0; i < FILEPROC_POOL_SIZE; i++) {
        if (!s_fp_used[i]) {
            s_fp_used[i] = 1;
            fp = &s_fp_pool[i];
            break;
        }
    }
    spinlock_unlock_irqrestore(&s_fp_pool_lock, irq);
    if (!fp) return nullptr;

    __builtin_memset(fp, 0, sizeof(*fp));
    fp->fp_signature = XIU_FILEPROC_MAGIC;
    fp->fp_flags     = flags | FP_READABLE | FP_WRITABLE;
    fp->fp_type      = DTYPE_SOCKET;
    fp->fp_socket    = so;
    fp->fp_offset    = 0;
    atomic_store_explicit(&fp->fp_refcount, 1, memory_order_relaxed);
    spinlock_init(&fp->fp_lock);
    return fp;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * fp_release — drop one reference to a fileproc.
 *
 * When refcount reaches zero the vnode iocount is decremented and the
 * pool slot is returned.
 * ═══════════════════════════════════════════════════════════════════════════ */
void fp_release(xiu_fileproc_t *fp) {
    if (!fp) return;
    XIU_ASSERT(fp->fp_signature == XIU_FILEPROC_MAGIC);

    u32 prev = atomic_fetch_sub_explicit(&fp->fp_refcount, 1,
                                          memory_order_acq_rel);
    if (prev > 1) return;

    // last reference — release resource
    if (fp->fp_type == DTYPE_SOCKET && fp->fp_socket) {
        extern xiu_error_t soclose(struct socket *so);
        soclose(fp->fp_socket);
        fp->fp_socket = nullptr;
    } else if (fp->fp_vnode) {
        vnode_t *vp = fp->fp_vnode;
        irq_flags_t virq = spinlock_lock_irqsave(&vp->v_lock);
        if (vp->v_iocount > 0) vp->v_iocount--;
        spinlock_unlock_irqrestore(&vp->v_lock, virq);
        fp->fp_vnode = nullptr;
    }

    fp->fp_signature = 0xDEADDEADDEADDEADULL;

    // return to pool
    usize idx = (usize)(fp - s_fp_pool);
    XIU_ASSERT(idx < FILEPROC_POOL_SIZE);

    irq_flags_t irq = spinlock_lock_irqsave(&s_fp_pool_lock);
    s_fp_used[idx] = 0;
    spinlock_unlock_irqrestore(&s_fp_pool_lock, irq);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * proc_fd_install — find lowest free fd, install fp, return fd number.
 *
 * Returns -1 if the FDT is full (XIU_PROC_MAX_FDS entries).
 * Called with proc NOT locked — we acquire p_fdlock internally.
 * ═══════════════════════════════════════════════════════════════════════════ */
int proc_fd_install(xiu_proc_t *p, xiu_fileproc_t *fp) {
    XIU_ASSERT(p  != nullptr);
    XIU_ASSERT(fp != nullptr);

    irq_flags_t irq = spinlock_lock_irqsave(&p->p_fdlock);

    int fd = -1;
    for (int i = 0; i < XIU_PROC_MAX_FDS; i++) {
        if (p->p_fd_table[i] == nullptr) {
            fp_retain(fp);           // fdt holds one reference
            p->p_fd_table[i] = fp;
            fd = i;
            break;
        }
    }

    spinlock_unlock_irqrestore(&p->p_fdlock, irq);

    if (fd < 0)
        kprintf("[fileproc] proc_fd_install: FDT full for pid=%u\n", p->p_pid);

    return fd;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * proc_fd_lookup — return the fileproc for fd, or NULL if invalid.
 *
 * Bumps refcount so the caller must call fp_release() when done.
 * ═══════════════════════════════════════════════════════════════════════════ */
xiu_fileproc_t *proc_fd_lookup(xiu_proc_t *p, int fd) {
    if (!p || fd < 0 || fd >= XIU_PROC_MAX_FDS) return nullptr;

    irq_flags_t irq = spinlock_lock_irqsave(&p->p_fdlock);
    xiu_fileproc_t *fp = p->p_fd_table[fd];
    if (fp) fp_retain(fp);
    spinlock_unlock_irqrestore(&p->p_fdlock, irq);

    return fp;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * proc_fd_close — remove fd from the FDT, release the FDT reference.
 * ═══════════════════════════════════════════════════════════════════════════ */
xiu_error_t proc_fd_close(xiu_proc_t *p, int fd) {
    if (!p || fd < 0 || fd >= XIU_PROC_MAX_FDS) return XIU_ERR_INVALID;

    irq_flags_t irq = spinlock_lock_irqsave(&p->p_fdlock);
    xiu_fileproc_t *fp = p->p_fd_table[fd];
    if (!fp) {
        spinlock_unlock_irqrestore(&p->p_fdlock, irq);
        return XIU_ERR_INVALID;
    }
    p->p_fd_table[fd] = nullptr;
    spinlock_unlock_irqrestore(&p->p_fdlock, irq);

    fp_release(fp);   // release FDT's reference
    return XIU_SUCCESS;
}
