/* =============================================================================
 * XIU Operating System — UNIX Anonymous Pipe Implementation
 * kernel/vfs/pipe.c
 * ============================================================================= */

#include <kernel/vfs_node.h>
#include <kernel/spinlock.h>
#include <kernel/uio.h>
#include <kernel/proc.h>
#include <kernel/panic.h>

extern void scheduler_yield(void);
extern xiu_error_t copyin(const void *uaddr, void *kaddr, usize len);
extern xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);
extern xiu_task_t *current_task(void);
extern xiu_error_t proc_signal(xiu_proc_t *proc, int sig);

#define PIPE_BUF_SIZE 65536
#define MAX_PIPES     32

typedef struct xiu_pipe {
    u8           buf[PIPE_BUF_SIZE];
    usize        head;   // write offset
    usize        tail;   // read offset
    usize        count;  // bytes currently in buffer
    u32          readers;
    u32          writers;
    bool         active;
    spinlock_t   lock;
    vnode_t      read_vn;
    vnode_t      write_vn;
} xiu_pipe_t;

static xiu_pipe_t s_pipes[MAX_PIPES];
static spinlock_t s_pipe_pool_lock = SPINLOCK_INIT;

// pipe read vnode operations
static xiu_error_t pipe_read_vop_read(vnode_t *vp, struct uio *uio, int flags,
                                      vfs_context_t *ctx) {
    (void)flags; (void)ctx;
    if (!vp || !uio || !uio->uio_buf || uio->uio_resid == 0) return XIU_SUCCESS;

    xiu_pipe_t *pipe = (xiu_pipe_t *)vp->v_data;
    if (!pipe) return XIU_ERR_INVALID;

    while (uio->uio_resid > 0) {
        irq_flags_t irq = spinlock_lock_irqsave(&pipe->lock);

        if (pipe->count > 0) {
            usize to_read = uio->uio_resid;
            if (to_read > pipe->count) to_read = pipe->count;

            // copy in up to two chunks to handle circular wrap-around
            usize first_chunk = to_read;
            if (pipe->tail + first_chunk > PIPE_BUF_SIZE) {
                first_chunk = PIPE_BUF_SIZE - pipe->tail;
            }
            usize second_chunk = to_read - first_chunk;

            char tmp[256];
            usize copied = 0;
            while (copied < first_chunk) {
                usize step = first_chunk - copied;
                if (step > sizeof(tmp)) step = sizeof(tmp);
                __builtin_memcpy(tmp, &pipe->buf[pipe->tail + copied], step);
                spinlock_unlock_irqrestore(&pipe->lock, irq);

                if (copyout(tmp, (void *)((uptr)uio->uio_buf + copied), step) != XIU_SUCCESS) {
                    return XIU_ERR_GENERIC;
                }
                irq = spinlock_lock_irqsave(&pipe->lock);
                copied += step;
            }

            if (second_chunk > 0) {
                copied = 0;
                while (copied < second_chunk) {
                    usize step = second_chunk - copied;
                    if (step > sizeof(tmp)) step = sizeof(tmp);
                    __builtin_memcpy(tmp, &pipe->buf[copied], step);
                    spinlock_unlock_irqrestore(&pipe->lock, irq);

                    if (copyout(tmp, (void *)((uptr)uio->uio_buf + first_chunk + copied), step) != XIU_SUCCESS) {
                        return XIU_ERR_GENERIC;
                    }
                    irq = spinlock_lock_irqsave(&pipe->lock);
                    copied += step;
                }
            }

            pipe->tail = (pipe->tail + to_read) % PIPE_BUF_SIZE;
            pipe->count -= to_read;
            spinlock_unlock_irqrestore(&pipe->lock, irq);

            uio->uio_buf = (void *)((uptr)uio->uio_buf + to_read);
            uio->uio_resid -= to_read;
            return XIU_SUCCESS;
        }

        // buffer is empty
        if (pipe->writers == 0) {
            spinlock_unlock_irqrestore(&pipe->lock, irq);
            return XIU_SUCCESS;
        }

        // writers still open: yield and wait for data
        spinlock_unlock_irqrestore(&pipe->lock, irq);
        scheduler_yield();
    }

    return XIU_SUCCESS;
}

static xiu_error_t pipe_read_vop_close(vnode_t *vp, int flags, vfs_context_t *ctx) {
    (void)flags; (void)ctx;
    if (!vp) return XIU_SUCCESS;
    xiu_pipe_t *pipe = (xiu_pipe_t *)vp->v_data;
    if (!pipe) return XIU_SUCCESS;

    irq_flags_t irq = spinlock_lock_irqsave(&pipe->lock);
    if (pipe->readers > 0) pipe->readers--;
    if (pipe->readers == 0 && pipe->writers == 0) {
        pipe->active = false;
    }
    spinlock_unlock_irqrestore(&pipe->lock, irq);
    return XIU_SUCCESS;
}

static vnode_ops_t s_pipe_read_ops = {
    .vop_name  = "pipe_read",
    .vop_read  = pipe_read_vop_read,
    .vop_close = pipe_read_vop_close
};

// pipe write vnode operations
static xiu_error_t pipe_write_vop_write(vnode_t *vp, struct uio *uio, int flags,
                                        vfs_context_t *ctx) {
    (void)flags; (void)ctx;
    if (!vp || !uio || !uio->uio_buf || uio->uio_resid == 0) return XIU_SUCCESS;

    xiu_pipe_t *pipe = (xiu_pipe_t *)vp->v_data;
    if (!pipe) return XIU_ERR_INVALID;

    while (uio->uio_resid > 0) {
        irq_flags_t irq = spinlock_lock_irqsave(&pipe->lock);

        if (pipe->readers == 0) {
            // no readers: send SIGPIPE and return EPIPE
            spinlock_unlock_irqrestore(&pipe->lock, irq);
            xiu_task_t *task = current_task();
            if (task && task->ta_proc) {
                proc_signal(task->ta_proc, 13);
            }
            return XIU_ERR_GENERIC; // -epipe
        }

        usize space = PIPE_BUF_SIZE - pipe->count;
        if (space > 0) {
            usize to_write = uio->uio_resid;
            if (to_write > space) to_write = space;

            usize first_chunk = to_write;
            if (pipe->head + first_chunk > PIPE_BUF_SIZE) {
                first_chunk = PIPE_BUF_SIZE - pipe->head;
            }
            usize second_chunk = to_write - first_chunk;

            char tmp[256];
            usize copied = 0;
            while (copied < first_chunk) {
                usize step = first_chunk - copied;
                if (step > sizeof(tmp)) step = sizeof(tmp);
                spinlock_unlock_irqrestore(&pipe->lock, irq);

                if (copyin((const void *)((uptr)uio->uio_buf + copied), tmp, step) != XIU_SUCCESS) {
                    return XIU_ERR_GENERIC;
                }
                irq = spinlock_lock_irqsave(&pipe->lock);
                __builtin_memcpy(&pipe->buf[pipe->head + copied], tmp, step);
                copied += step;
            }

            if (second_chunk > 0) {
                copied = 0;
                while (copied < second_chunk) {
                    usize step = second_chunk - copied;
                    if (step > sizeof(tmp)) step = sizeof(tmp);
                    spinlock_unlock_irqrestore(&pipe->lock, irq);

                    if (copyin((const void *)((uptr)uio->uio_buf + first_chunk + copied), tmp, step) != XIU_SUCCESS) {
                        return XIU_ERR_GENERIC;
                    }
                    irq = spinlock_lock_irqsave(&pipe->lock);
                    __builtin_memcpy(&pipe->buf[copied], tmp, step);
                    copied += step;
                }
            }

            pipe->head = (pipe->head + to_write) % PIPE_BUF_SIZE;
            pipe->count += to_write;
            spinlock_unlock_irqrestore(&pipe->lock, irq);

            uio->uio_buf = (void *)((uptr)uio->uio_buf + to_write);
            uio->uio_resid -= to_write;
            continue;
        }

        // buffer is full: yield and wait for readers to consume
        spinlock_unlock_irqrestore(&pipe->lock, irq);
        scheduler_yield();
    }

    return XIU_SUCCESS;
}

static xiu_error_t pipe_write_vop_close(vnode_t *vp, int flags, vfs_context_t *ctx) {
    (void)flags; (void)ctx;
    if (!vp) return XIU_SUCCESS;
    xiu_pipe_t *pipe = (xiu_pipe_t *)vp->v_data;
    if (!pipe) return XIU_SUCCESS;

    irq_flags_t irq = spinlock_lock_irqsave(&pipe->lock);
    if (pipe->writers > 0) pipe->writers--;
    if (pipe->readers == 0 && pipe->writers == 0) {
        pipe->active = false;
    }
    spinlock_unlock_irqrestore(&pipe->lock, irq);
    return XIU_SUCCESS;
}

static vnode_ops_t s_pipe_write_ops = {
    .vop_name  = "pipe_write",
    .vop_write = pipe_write_vop_write,
    .vop_close = pipe_write_vop_close
};

// pipe_create
xiu_error_t pipe_create(vnode_t **read_vp_out, vnode_t **write_vp_out) {
    if (!read_vp_out || !write_vp_out) return XIU_ERR_INVALID;

    irq_flags_t irq = spinlock_lock_irqsave(&s_pipe_pool_lock);
    xiu_pipe_t *pipe = nullptr;

    for (int i = 0; i < MAX_PIPES; i++) {
        if (!s_pipes[i].active) {
            pipe = &s_pipes[i];
            break;
        }
    }

    if (!pipe) {
        spinlock_unlock_irqrestore(&s_pipe_pool_lock, irq);
        return XIU_ERR_NOMEM;
    }

    pipe->head = 0;
    pipe->tail = 0;
    pipe->count = 0;
    pipe->readers = 1;
    pipe->writers = 1;
    pipe->active = true;
    spinlock_init(&pipe->lock);

    // setup read vnode
    __builtin_memset(&pipe->read_vn, 0, sizeof(vnode_t));
    pipe->read_vn.v_signature = XIU_VNODE_MAGIC;
    pipe->read_vn.v_type = VFIFO;
    pipe->read_vn.v_op = &s_pipe_read_ops;
    pipe->read_vn.v_data = pipe;
    pipe->read_vn.v_usecount = 1;
    __builtin_strncpy(pipe->read_vn.v_name, "pipe_r", sizeof(pipe->read_vn.v_name) - 1);

    // setup write vnode
    __builtin_memset(&pipe->write_vn, 0, sizeof(vnode_t));
    pipe->write_vn.v_signature = XIU_VNODE_MAGIC;
    pipe->write_vn.v_type = VFIFO;
    pipe->write_vn.v_op = &s_pipe_write_ops;
    pipe->write_vn.v_data = pipe;
    pipe->write_vn.v_usecount = 1;
    __builtin_strncpy(pipe->write_vn.v_name, "pipe_w", sizeof(pipe->write_vn.v_name) - 1);

    spinlock_unlock_irqrestore(&s_pipe_pool_lock, irq);

    *read_vp_out = &pipe->read_vn;
    *write_vp_out = &pipe->write_vn;
    return XIU_SUCCESS;
}
