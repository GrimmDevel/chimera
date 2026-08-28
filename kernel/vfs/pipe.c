/* =============================================================================
 * Chimera Operating System — UNIX Anonymous Pipe Implementation
 * kernel/vfs/pipe.c
 * ============================================================================= */

#include <kernel/vfs_node.h>
#include <kernel/spinlock.h>
#include <kernel/uio.h>
#include <kernel/proc.h>
#include <kernel/panic.h>
#include <kernel/wait_queue.h>

extern chimera_error_t copyin(const void *uaddr, void *kaddr, usize len);
extern chimera_error_t copyout(const void *kaddr, void *uaddr, usize len);
extern chimera_task_t *current_task(void);
extern chimera_error_t proc_signal(chimera_proc_t *proc, int sig);

#define PIPE_BUF_SIZE 65536
#define MAX_PIPES     32

typedef struct chimera_pipe {
    u8           buf[PIPE_BUF_SIZE];
    usize        head;   // write offset
    usize        tail;   // read offset
    usize        count;  // bytes currently in buffer
    u32          readers;
    u32          writers;
    bool         active;
    spinlock_t   lock;
    wait_queue_t r_wq;
    wait_queue_t w_wq;
    vnode_t      read_vn;
    vnode_t      write_vn;
} chimera_pipe_t;

static chimera_pipe_t s_pipes[MAX_PIPES];
static spinlock_t s_pipe_pool_lock = SPINLOCK_INIT;

// pipe read vnode operations
static chimera_error_t pipe_read_vop_read(vnode_t *vp, struct uio *uio, int flags,
                                          vfs_context_t *ctx) {
    (void)flags; (void)ctx;
    if (!vp || !uio || !uio->uio_buf || uio->uio_resid == 0) return CHIMERA_SUCCESS;

    chimera_pipe_t *pipe = (chimera_pipe_t *)vp->v_data;
    if (!pipe) return CHIMERA_ERR_INVALID;

    usize total_read = 0;

    while (uio->uio_resid > 0) {
        char tmp[256];
        usize to_read = 0;

        irq_flags_t irq = spinlock_lock_irqsave(&pipe->lock);

        if (pipe->count == 0) {
            if (pipe->writers == 0 || total_read > 0) {
                // EOF or already read partial data
                spinlock_unlock_irqrestore(&pipe->lock, irq);
                return CHIMERA_SUCCESS;
            }
            wait_queue_sleep_irqrestore(&pipe->r_wq, &pipe->lock, irq);
            continue;
        }

        to_read = uio->uio_resid < sizeof(tmp) ? uio->uio_resid : sizeof(tmp);
        if (to_read > pipe->count) to_read = pipe->count;

        for (usize i = 0; i < to_read; i++) {
            tmp[i] = (char)pipe->buf[(pipe->tail + i) % PIPE_BUF_SIZE];
        }
        pipe->tail = (pipe->tail + to_read) % PIPE_BUF_SIZE;
        pipe->count -= to_read;

        wait_queue_wakeup_one(&pipe->w_wq);
        spinlock_unlock_irqrestore(&pipe->lock, irq);

        if (copyout(tmp, uio->uio_buf, to_read) != CHIMERA_SUCCESS) {
            return CHIMERA_ERR_GENERIC;
        }

        uio->uio_buf = (void *)((uptr)uio->uio_buf + to_read);
        uio->uio_resid -= to_read;
        total_read += to_read;
    }

    return CHIMERA_SUCCESS;
}

static chimera_error_t pipe_read_vop_close(vnode_t *vp, int flags, vfs_context_t *ctx) {
    (void)flags; (void)ctx;
    if (!vp) return CHIMERA_SUCCESS;
    chimera_pipe_t *pipe = (chimera_pipe_t *)vp->v_data;
    if (!pipe) return CHIMERA_SUCCESS;

    irq_flags_t irq = spinlock_lock_irqsave(&pipe->lock);
    if (pipe->readers > 0) pipe->readers--;
    if (pipe->readers == 0 && pipe->writers == 0) {
        pipe->active = false;
    }
    // wake any writers blocked on full pipe
    wait_queue_wakeup_all(&pipe->w_wq);
    spinlock_unlock_irqrestore(&pipe->lock, irq);
    return CHIMERA_SUCCESS;
}

static vnode_ops_t s_pipe_read_ops = {
    .vop_name  = "pipe_read",
    .vop_read  = pipe_read_vop_read,
    .vop_close = pipe_read_vop_close
};

// pipe write vnode operations
static chimera_error_t pipe_write_vop_write(vnode_t *vp, struct uio *uio, int flags,
                                            vfs_context_t *ctx) {
    (void)flags; (void)ctx;
    if (!vp || !uio || !uio->uio_buf || uio->uio_resid == 0) return CHIMERA_SUCCESS;

    chimera_pipe_t *pipe = (chimera_pipe_t *)vp->v_data;
    if (!pipe) return CHIMERA_ERR_INVALID;

    while (uio->uio_resid > 0) {
        char tmp[256];
        usize chunk_to_copy = uio->uio_resid < sizeof(tmp) ? uio->uio_resid : sizeof(tmp);
        if (copyin(uio->uio_buf, tmp, chunk_to_copy) != CHIMERA_SUCCESS) {
            return CHIMERA_ERR_GENERIC;
        }

        irq_flags_t irq = spinlock_lock_irqsave(&pipe->lock);

        if (pipe->readers == 0) {
            spinlock_unlock_irqrestore(&pipe->lock, irq);
            chimera_task_t *task = current_task();
            if (task && task->ta_proc) {
                proc_signal(task->ta_proc, 13);
            }
            return CHIMERA_ERR_GENERIC; // -EPIPE
        }

        if (pipe->count >= PIPE_BUF_SIZE) {
            // buffer is full: sleep on write wait queue
            wait_queue_sleep_irqrestore(&pipe->w_wq, &pipe->lock, irq);
            continue;
        }

        usize space = PIPE_BUF_SIZE - pipe->count;
        usize to_write = chunk_to_copy < space ? chunk_to_copy : space;

        // copy into circular buffer
        for (usize i = 0; i < to_write; i++) {
            pipe->buf[(pipe->head + i) % PIPE_BUF_SIZE] = (u8)tmp[i];
        }
        pipe->head = (pipe->head + to_write) % PIPE_BUF_SIZE;
        pipe->count += to_write;

        wait_queue_wakeup_one(&pipe->r_wq);
        spinlock_unlock_irqrestore(&pipe->lock, irq);

        uio->uio_buf = (void *)((uptr)uio->uio_buf + to_write);
        uio->uio_resid -= to_write;
    }

    return CHIMERA_SUCCESS;
}

static chimera_error_t pipe_write_vop_close(vnode_t *vp, int flags, vfs_context_t *ctx) {
    (void)flags; (void)ctx;
    if (!vp) return CHIMERA_SUCCESS;
    chimera_pipe_t *pipe = (chimera_pipe_t *)vp->v_data;
    if (!pipe) return CHIMERA_SUCCESS;

    irq_flags_t irq = spinlock_lock_irqsave(&pipe->lock);
    if (pipe->writers > 0) pipe->writers--;
    if (pipe->readers == 0 && pipe->writers == 0) {
        pipe->active = false;
    }
    // wake any readers blocked on empty pipe so they see EOF (writers==0)
    wait_queue_wakeup_all(&pipe->r_wq);
    spinlock_unlock_irqrestore(&pipe->lock, irq);
    return CHIMERA_SUCCESS;
}

static vnode_ops_t s_pipe_write_ops = {
    .vop_name  = "pipe_write",
    .vop_write = pipe_write_vop_write,
    .vop_close = pipe_write_vop_close
};

// pipe_create
chimera_error_t pipe_create(vnode_t **read_vp_out, vnode_t **write_vp_out) {
    if (!read_vp_out || !write_vp_out) return CHIMERA_ERR_INVALID;

    irq_flags_t irq = spinlock_lock_irqsave(&s_pipe_pool_lock);
    chimera_pipe_t *pipe = nullptr;

    for (int i = 0; i < MAX_PIPES; i++) {
        if (!s_pipes[i].active) {
            pipe = &s_pipes[i];
            break;
        }
    }

    if (!pipe) {
        spinlock_unlock_irqrestore(&s_pipe_pool_lock, irq);
        return CHIMERA_ERR_NOMEM;
    }

    pipe->head = 0;
    pipe->tail = 0;
    pipe->count = 0;
    pipe->readers = 1;
    pipe->writers = 1;
    pipe->active = true;
    spinlock_init(&pipe->lock);
    wait_queue_init(&pipe->r_wq);
    wait_queue_init(&pipe->w_wq);

    // setup read vnode
    __builtin_memset(&pipe->read_vn, 0, sizeof(vnode_t));
    pipe->read_vn.v_signature = CHIMERA_VNODE_MAGIC;
    pipe->read_vn.v_type = VFIFO;
    pipe->read_vn.v_op = &s_pipe_read_ops;
    pipe->read_vn.v_data = pipe;
    pipe->read_vn.v_usecount = 1;
    __builtin_strncpy(pipe->read_vn.v_name, "pipe_r", sizeof(pipe->read_vn.v_name) - 1);

    // setup write vnode
    __builtin_memset(&pipe->write_vn, 0, sizeof(vnode_t));
    pipe->write_vn.v_signature = CHIMERA_VNODE_MAGIC;
    pipe->write_vn.v_type = VFIFO;
    pipe->write_vn.v_op = &s_pipe_write_ops;
    pipe->write_vn.v_data = pipe;
    pipe->write_vn.v_usecount = 1;
    __builtin_strncpy(pipe->write_vn.v_name, "pipe_w", sizeof(pipe->write_vn.v_name) - 1);

    spinlock_unlock_irqrestore(&s_pipe_pool_lock, irq);

    *read_vp_out = &pipe->read_vn;
    *write_vp_out = &pipe->write_vn;
    return CHIMERA_SUCCESS;
}

i16 pipe_poll(vnode_t *vp, i16 events) {
    if (!vp || !vp->v_data) return 0x0020; // POLLNVAL
    chimera_pipe_t *pipe = (chimera_pipe_t *)vp->v_data;
    i16 revents = 0;

    irq_flags_t irq = spinlock_lock_irqsave(&pipe->lock);
    bool is_read = (vp->v_op == &s_pipe_read_ops);

    if (is_read) {
        if (events & 0x0001) { // POLLIN
            if (pipe->count > 0 || pipe->writers == 0) {
                revents |= 0x0001;
            }
        }
        if (pipe->writers == 0) {
            revents |= 0x0010; // POLLHUP
        }
    } else {
        if (events & 0x0004) { // POLLOUT
            if (pipe->readers > 0 && (PIPE_BUF_SIZE - pipe->count > 0)) {
                revents |= 0x0004;
            }
        }
        if (pipe->readers == 0) {
            revents |= 0x0008 | 0x0010; // POLLERR | POLLHUP
        }
    }
    spinlock_unlock_irqrestore(&pipe->lock, irq);
    return revents;
}

