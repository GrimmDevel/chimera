/* =============================================================================
 * Chimera Operating System — PTY (Pseudo-Terminal) Implementation
 * kernel/vfs/pty.c
 *
 * Architecture:
 *   /dev/ptmx  (master) — opened by Terminal.app
 *       write → master_buf → slave_read  (Terminal sends keys → dash reads)
 *       read  ← slave_buf ← slave_write (Terminal sees dash output)
 *
 *   /dev/pts/0 (slave)  — dash's fd 0 + fd 1
 *       read  ← master_buf ← master_write (dash reads keyboard input)
 *       write → slave_buf  → master_read  (dash writes output → Terminal)
 *
 * Blocking: slave_read() and master_read() yield until data arrives.
 * =============================================================================
 */

#include <kernel/proc.h>
#include <kernel/spinlock.h>
#include <kernel/uio.h>
#include <kernel/vfs_node.h>
#include <kernel/wait_queue.h>

extern void kprintf(const char *fmt, ...);

#define PTY_BUF_SIZE 4096

typedef struct pty {
  char master_buf[PTY_BUF_SIZE];
  u32 master_head;
  u32 master_tail;

  char slave_buf[PTY_BUF_SIZE];
  u32 slave_head;
  u32 slave_tail;

  char line_buf[PTY_BUF_SIZE];
  u32 line_len;
  bool line_ready;

  spinlock_t lock;
  wait_queue_t master_wq;
  wait_queue_t slave_wq;
} pty_t;

static pty_t g_pty0;

// ring buffer helpers
static inline u32 ring_used(u32 head, u32 tail, u32 cap) {
  return (head >= tail) ? (head - tail) : (cap - tail + head);
}
static inline u32 ring_free(u32 head, u32 tail, u32 cap) {
  return cap - 1 - ring_used(head, tail, cap);
}

static usize ring_write(char *buf, u32 *head, u32 tail, u32 cap,
                        const char *src, usize len) {
  usize avail = ring_free(*head, tail, cap);
  if (len > avail)
    len = avail;
  for (usize i = 0; i < len; i++) {
    buf[*head] = src[i];
    *head = (*head + 1) % cap;
  }
  return len;
}

static usize ring_read(const char *buf, u32 head, u32 *tail, u32 cap, char *dst,
                       usize len) {
  usize avail = ring_used(head, *tail, cap);
  if (len > avail)
    len = avail;
  for (usize i = 0; i < len; i++) {
    dst[i] = buf[*tail];
    *tail = (*tail + 1) % cap;
  }
  return len;
}

// master operations
static chimera_error_t pty_master_read(struct vnode *vp, struct uio *uio,
                                   int ioflags, vfs_context_t *ctx) {
  (void)vp;
  (void)ioflags;
  (void)ctx;
  if (!uio || uio->uio_resid == 0)
    return CHIMERA_SUCCESS;

  char tmp[512];
  extern chimera_error_t copyout(const void *kaddr, void *uaddr, usize len);

  spinlock_lock(&g_pty0.lock);
  usize avail = ring_used(g_pty0.slave_head, g_pty0.slave_tail, PTY_BUF_SIZE);
  if (avail > 0) {
    usize to_read = (uio->uio_resid < avail) ? uio->uio_resid : avail;
    if (to_read > sizeof(tmp))
      to_read = sizeof(tmp);

    usize n = ring_read(g_pty0.slave_buf, g_pty0.slave_head, &g_pty0.slave_tail,
                        PTY_BUF_SIZE, tmp, to_read);
    spinlock_unlock(&g_pty0.lock);

    if (copyout(tmp, (void *)uio->uio_buf, n) == CHIMERA_SUCCESS) {
      uio->uio_buf = (void *)((uptr)uio->uio_buf + n);
      uio->uio_resid -= n;
    }
    return CHIMERA_SUCCESS;
  }
  spinlock_unlock(&g_pty0.lock);
  return CHIMERA_SUCCESS;
}

// terminal writes keyboard input → goes through line discipline → dash reads it
static chimera_error_t pty_master_write(struct vnode *vp, struct uio *uio,
                                    int ioflags, vfs_context_t *ctx) {
  (void)vp;
  (void)ioflags;
  (void)ctx;
  if (!uio || !uio->uio_buf || uio->uio_resid == 0)
    return CHIMERA_SUCCESS;

  char tmp[512];
  extern chimera_error_t copyin(const void *uaddr, void *kaddr, usize len);

  usize to_write =
      (uio->uio_resid > sizeof(tmp)) ? sizeof(tmp) : uio->uio_resid;
  if (copyin((const void *)uio->uio_buf, tmp, to_write) != CHIMERA_SUCCESS)
    return CHIMERA_ERR_GENERIC;

  spinlock_lock(&g_pty0.lock);

  // process each character through line discipline
  for (usize i = 0; i < to_write; i++) {
    u8 ch = tmp[i];

    // handle backspace/delete
    if (ch == 0x08 || ch == 0x7f) {
      if (g_pty0.line_len > 0) {
        g_pty0.line_len--;
        // echo erase sequence: \b \b
        const char erase[] = {0x08, ' ', 0x08};
        ring_write(g_pty0.slave_buf, &g_pty0.slave_head, g_pty0.slave_tail,
                   PTY_BUF_SIZE, erase, 3);
      }
      continue;
    }

    // add character to line buffer
    if (g_pty0.line_len < PTY_BUF_SIZE - 1) {
      g_pty0.line_buf[g_pty0.line_len++] = ch;

      // echo character back to terminal
      ring_write(g_pty0.slave_buf, &g_pty0.slave_head, g_pty0.slave_tail,
                 PTY_BUF_SIZE, (const char *)&ch, 1);

      if (ch == '\n' || ch == '\r') {
        ring_write(g_pty0.master_buf, &g_pty0.master_head, g_pty0.master_tail,
                   PTY_BUF_SIZE, g_pty0.line_buf, g_pty0.line_len);
        g_pty0.line_len = 0;
        g_pty0.line_ready = true;
      }
    }
  }

  uio->uio_buf = (void *)((uptr)uio->uio_buf + to_write);
  uio->uio_resid -= to_write;

  wait_queue_wakeup_one(&g_pty0.slave_wq);
  wait_queue_wakeup_one(&g_pty0.master_wq);
  spinlock_unlock(&g_pty0.lock);

  return CHIMERA_SUCCESS;
}

static chimera_error_t pty_slave_read(struct vnode *vp, struct uio *uio,
                                  int ioflags, vfs_context_t *ctx) {
  (void)vp;
  (void)ioflags;
  (void)ctx;
  if (!uio || uio->uio_resid == 0)
    return CHIMERA_SUCCESS;

  char tmp[512];
  extern chimera_error_t copyout(const void *kaddr, void *uaddr, usize len);

  // block until master has sent at least one byte
  for (;;) {
    spinlock_lock(&g_pty0.lock);
    usize avail =
        ring_used(g_pty0.master_head, g_pty0.master_tail, PTY_BUF_SIZE);
    if (avail > 0) {
      usize to_read = (uio->uio_resid < avail) ? uio->uio_resid : avail;
      if (to_read > sizeof(tmp))
        to_read = sizeof(tmp);

      usize n = ring_read(g_pty0.master_buf, g_pty0.master_head,
                          &g_pty0.master_tail, PTY_BUF_SIZE, tmp, to_read);
      spinlock_unlock(&g_pty0.lock);

      if (copyout(tmp, (void *)uio->uio_buf, n) == CHIMERA_SUCCESS) {
        uio->uio_buf = (void *)((uptr)uio->uio_buf + n);
        uio->uio_resid -= n;
      }
      return CHIMERA_SUCCESS;
    }
    wait_queue_sleep(&g_pty0.slave_wq, &g_pty0.lock);
  }
}

// dash writes stdout → goes into slave_buf → Terminal reads and renders it
static chimera_error_t pty_slave_write(struct vnode *vp, struct uio *uio,
                                   int ioflags, vfs_context_t *ctx) {
  (void)vp;
  (void)ioflags;
  (void)ctx;
  if (!uio || !uio->uio_buf || uio->uio_resid == 0)
    return CHIMERA_SUCCESS;

  char tmp[512];
  extern chimera_error_t copyin(const void *uaddr, void *kaddr, usize len);

  usize to_write =
      (uio->uio_resid > sizeof(tmp)) ? sizeof(tmp) : uio->uio_resid;
  if (copyin((const void *)uio->uio_buf, tmp, to_write) != CHIMERA_SUCCESS)
    return CHIMERA_ERR_GENERIC;

  spinlock_lock(&g_pty0.lock);
  usize n = ring_write(g_pty0.slave_buf, &g_pty0.slave_head, g_pty0.slave_tail,
                       PTY_BUF_SIZE, tmp, to_write);
  uio->uio_buf = (void *)((uptr)uio->uio_buf + n);
  uio->uio_resid -= n;

  wait_queue_wakeup_one(&g_pty0.master_wq);
  spinlock_unlock(&g_pty0.lock);

  return CHIMERA_SUCCESS;
}

static chimera_error_t pty_ioctl(struct vnode *vp, u64 cmd, chimera_vaddr_t arg,
                             vfs_context_t *ctx) {
  (void)vp;
  (void)ctx;

  // tiocgwinsz
  if (cmd == 0x5413 && arg) {
    u16 ws[4] = {25, 80, 0, 0};
    extern chimera_error_t copyout(const void *kaddr, void *uaddr, usize len);
    return copyout(ws, (void *)arg, sizeof(ws));
  }

  // stage 1 TTY compatibility: treat other termios/job-control ioctls as OK.
  return CHIMERA_SUCCESS;
}

vnode_ops_t s_pty_master_ops = {.vop_name = "pty_master",
                                .vop_read = pty_master_read,
                                .vop_write = pty_master_write,
                                .vop_ioctl = pty_ioctl};

vnode_ops_t s_pty_slave_ops = {.vop_name = "pty_slave",
                               .vop_read = pty_slave_read,
                               .vop_write = pty_slave_write,
                               .vop_ioctl = pty_ioctl};

void pty_init(void) {
  spinlock_init(&g_pty0.lock);
  wait_queue_init(&g_pty0.master_wq);
  wait_queue_init(&g_pty0.slave_wq);
  g_pty0.master_head = g_pty0.master_tail = 0;
  g_pty0.slave_head = g_pty0.slave_tail = 0;
  g_pty0.line_len = 0;
  g_pty0.line_ready = false;
}

i16 pty_master_poll(i16 events) {
  i16 revents = 0;
  spinlock_lock(&g_pty0.lock);
  if ((events & 0x0001) &&
      ring_used(g_pty0.slave_head, g_pty0.slave_tail, PTY_BUF_SIZE) > 0) {
    revents |= 0x0001; // POLLIN
  }
  if (events & 0x0004) {
    revents |= 0x0004; // POLLOUT
  }
  spinlock_unlock(&g_pty0.lock);
  return revents;
}

i16 pty_slave_poll(i16 events) {
  i16 revents = 0;
  spinlock_lock(&g_pty0.lock);
  if ((events & 0x0001) &&
      ring_used(g_pty0.master_head, g_pty0.master_tail, PTY_BUF_SIZE) > 0) {
    revents |= 0x0001; // POLLIN
  }
  if ((events & 0x0004) &&
      ring_free(g_pty0.slave_head, g_pty0.slave_tail, PTY_BUF_SIZE) > 0) {
    revents |= 0x0004; // POLLOUT
  }
  spinlock_unlock(&g_pty0.lock);
  return revents;
}
