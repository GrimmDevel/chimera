/* =============================================================================
 * XIU Operating System — System Call Dispatcher
 * kernel/bsd/syscall.c
 * =============================================================================
 */

#include <kernel/bsd_syscall_xnu.h>
#include <kernel/fileproc.h>
#include <kernel/iokit_xnu.h>
#include <kernel/ipc_message.h>
#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/syscall.h>
#include <kernel/uio.h>

#include <kernel/fb.h>
#include <kernel/mach_o.h>
#include <kernel/vfs_node.h>
#include <kernel/xiu_types.h>
#include <net/protocols.h>
#include <net/socket.h>

extern xiu_error_t copyin(const void *uaddr, void *kaddr, usize len);
extern xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);

// current task helper

// vfs_lookup forward declaration
extern xiu_error_t vfs_lookup(const char *path, vnode_t **vp_out);
extern xiu_error_t vfs_readdir_flat(vnode_t *dvp, u32 index, char *name_out,
                                    usize name_cap, vnode_t **child_out);
extern const char *vfs_path_for_vnode(vnode_t *vp);
extern u64 pmap_clone_user_space(u64 src_pml4_phys);
extern void task_switch_to_user_frame(uptr entry, uptr stack, void *frame,
                                      u64 rax);
extern void thread_init_fork_stack(xiu_thread_t *th, void *entry, void *stack);
extern void scheduler_yield(void);

static u64 g_syscall_frame;

typedef struct syscall_user_frame {
  u64 r15, r14, r13, r12, rbx, rbp;
  u64 rip;
  u64 rflags;
  u64 rsp; // user RSP - pushed FIRST in syscall_entry.S
} syscall_user_frame_t;

typedef struct xiu_user_dirent {
  u64 d_ino;
  u64 d_seekoff;
  u16 d_reclen;
  u16 d_namlen;
  u8 d_type;
  char d_name[1024];
} xiu_user_dirent_t;

typedef struct XIU_PACKED xiu_user_stat {
  u32 st_dev;
  u16 st_mode;
  u16 st_nlink;
  u64 st_ino;
  u32 st_uid;
  u32 st_gid;
  u32 st_rdev;
  u32 _pad;
  struct { i64 tv_sec; i64 tv_nsec; } st_atimespec;
  struct { i64 tv_sec; i64 tv_nsec; } st_mtimespec;
  struct { i64 tv_sec; i64 tv_nsec; } st_ctimespec;
  struct { i64 tv_sec; i64 tv_nsec; } st_birthtimespec;
  i64 st_size;
  i64 st_blocks;
  i32 st_blksize;
  u32 st_flags;
  u32 st_gen;
  i32 st_lspare;
  i64 st_qspare[2];
} xiu_user_stat_t;

static const char *normalize_path(const char *path) {
  if (!path || path[0] == '\0' || __builtin_strcmp(path, ".") == 0)
    return "/";
  return path;
}

static u32 vnode_dtype(vnode_t *vp) {
  if (!vp)
    return 0;
  if (vp->v_type == VDIR)
    return 4;
  if (vp->v_type == VCHR)
    return 2;
  if (vp->v_type == VREG)
    return 8;
  return 0;
}

static u16 vnode_mode(vnode_t *vp) {
  u16 perm = 0644;
  if (!vp)
    return (u16)(0100000 | perm);
  if (vp->v_type == VDIR)
    return (u16)(0040000 | 0755);
  if (vp->v_type == VCHR)
    return (u16)(0020000 | 0666);
  if (vp->v_type == VBLK)
    return (u16)(0060000 | 0660);
  if (vp->v_type == VLNK)
    return (u16)(0120000 | 0777);
  if (vp->v_type == VSOCK)
    return (u16)(0140000 | 0666);
  if (vp->v_type == VFIFO)
    return (u16)(0010000 | 0666);
  return (u16)(0100000 | perm);
}

// sys_log
static i64 sys_log(u64 ptr, u64 len, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)len;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!ptr)
    return -1;
  char kbuf[256];
  if (copyin((const void *)ptr, kbuf, sizeof(kbuf) - 1) == XIU_SUCCESS) {
    kbuf[sizeof(kbuf) - 1] = '\0';
    kprintf("%s\n", kbuf);
  }
  return 0;
}

/* ── sys_exit ────────────────────────────────────────────────────────────── *
 * Marks the task as exited, frees descriptors, unlinks from scheduler, and
 * yields.
 * ───────────────────────────────────────────────────────────────────────────
 */
void sys_exit_direct(u64 code) {
  xiu_task_t *task = current_task();
  dprintf("[SYSCALL] exit(%llu) pid=%u\n", (unsigned long long)code,
          task && task->ta_proc ? task->ta_proc->p_pid : 0);
  if (task && task->ta_proc)
    proc_mark_exited(task->ta_proc, (u32)code);
  xiu_thread_t *th = current_thread();
  if (th) {
    th->th_state = THREAD_STATE_HALTED;
    scheduler_remove_thread(th);
  }
  extern void scheduler_yield(void);
  while (1)
    scheduler_yield();
}

static i64 sys_exit(u64 code, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  sys_exit_direct(code);
  return 0;
}

static void resolve_relative_path(xiu_proc_t *proc, const char *path,
                                  char *out_buf, usize out_max);

// sys_chdir
static i64 sys_chdir(u64 path_ptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  char path[256];
  if (copyin((const void *)path_ptr, path, sizeof(path)) != XIU_SUCCESS)
    return -1;
  path[sizeof(path) - 1] = '\0';

  dprintf("[SYSCALL] chdir(%s)\n", path);

  char norm_path[256];
  resolve_relative_path(proc, path, norm_path, sizeof(norm_path));

  vnode_t *new_cwd = nullptr;
  if (vfs_lookup(norm_path, &new_cwd) != XIU_SUCCESS || !new_cwd) {
    return -1; // ENOENT
  }

  // verify it's a directory
  if (new_cwd->v_type != VDIR) {
    return -20; // ENOTDIR
  }

  // update CWD
  irq_flags_t irq = spinlock_lock_irqsave(&proc->p_lock);
  proc->p_cwd = new_cwd;
  spinlock_unlock_irqrestore(&proc->p_lock, irq);

  return 0;
}

/* ── sys_write ───────────────────────────────────────────────────────────── *
 * Routes write() through VFS FDT exactly like sys_read.
 * Falls back to serial for fd 1/2 when the process has no FDT entry
 * (e.g. early startup before dash opens /dev/pts/0).
 * ───────────────────────────────────────────────────────────────────────────
 */
static i64 sys_write(u64 fd, u64 buf, u64 len, u64 a4, u64 a5, u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  int ifd = (int)fd;

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;

  if (proc) {
    xiu_fileproc_t *fp = proc_fd_lookup(proc, ifd);
    if (fp) {
      if (fp->fp_type == DTYPE_SOCKET && fp->fp_socket) {
        xiu_error_t err =
            sosend(fp->fp_socket, nullptr, (const void *)buf, len, 0);
        fp_release(fp);
        return (err == XIU_SUCCESS) ? (i64)len : -1;
      }
      vnode_t *vp = fp->fp_vnode;
      if (vp && vp->v_op && vp->v_op->vop_write) {
        struct uio uio;
        uio.uio_buf = (void *)buf;
        uio.uio_resid = len;
        uio.uio_offset = fp->fp_offset;
        xiu_error_t err = vp->v_op->vop_write(vp, &uio, 0, nullptr);
        usize written = len - uio.uio_resid;
        irq_flags_t irq = spinlock_lock_irqsave(&fp->fp_lock);
        fp->fp_offset += written;
        spinlock_unlock_irqrestore(&fp->fp_lock, irq);
        fp_release(fp);
        return (err == XIU_SUCCESS) ? (i64)written : -1;
      }
      fp_release(fp);
    }
  }

  // fallback to direct console write
  if (fd == 1 || fd == 2) {
    extern void console_write(const char *buf, usize len);
    console_write((const char *)buf, len);
    return (i64)len;
  }
  return -1;
}

static i64 sys_lseek(u64 fd_u, u64 offset_u, u64 whence_u, u64 a4, u64 a5,
                     u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  int fd = (int)fd_u;
  i64 offset = (i64)offset_u;
  int whence = (int)whence_u;

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || fd < 0 || fd >= XIU_PROC_MAX_FDS)
    return -9; // ebadf

  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp)
    return -9; // ebadf

  vnode_t *vp = fp->fp_vnode;
  if (!vp) {
    fp_release(fp);
    return -9; // ebadf
  }

  if (vp->v_type == VFIFO || vp->v_type == VSOCK || vp->v_type == VCHR) {
    fp_release(fp);
    return -29; // espipe
  }

  i64 new_offset = 0;
  irq_flags_t irq = spinlock_lock_irqsave(&fp->fp_lock);

  if (whence == 0) {
    new_offset = offset;
  } else if (whence == 1) {
    new_offset = (i64)fp->fp_offset + offset;
  } else if (whence == 2) {
    vattr_t attr;
    __builtin_memset(&attr, 0, sizeof(attr));
    if (vp->v_op && vp->v_op->vop_getattr) {
      vp->v_op->vop_getattr(vp, &attr, nullptr);
      new_offset = (i64)attr.va_size + offset;
    } else {
      new_offset = (i64)fp->fp_offset + offset;
    }
  } else {
    spinlock_unlock_irqrestore(&fp->fp_lock, irq);
    fp_release(fp);
    return -22; // einval
  }

  if (new_offset < 0) {
    spinlock_unlock_irqrestore(&fp->fp_lock, irq);
    fp_release(fp);
    return -22; // einval
  }

  fp->fp_offset = (u64)new_offset;
  spinlock_unlock_irqrestore(&fp->fp_lock, irq);
  fp_release(fp);

  return new_offset;
}

// sys_getpid
static i64 sys_getpid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  return (t && t->ta_proc) ? (i64)t->ta_proc->p_pid : 0;
}

static i64 sys_fcntl(u64 fd_u, u64 cmd_u, u64 arg, u64 a4, u64 a5, u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  int fd = (int)fd_u;
  int cmd = (int)cmd_u;

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || fd < 0 || fd >= XIU_PROC_MAX_FDS)
    return -1;

  enum {
    XIU_F_DUPFD = 0,
    XIU_F_GETFD = 1,
    XIU_F_SETFD = 2,
    XIU_F_GETFL = 3,
    XIU_F_SETFL = 4,
  };

  if (cmd == XIU_F_DUPFD) {
    int start = (int)arg;
    if (start < 0)
      start = 0;
    if (start >= XIU_PROC_MAX_FDS)
      return -1;

    irq_flags_t irq = spinlock_lock_irqsave(&proc->p_fdlock);
    xiu_fileproc_t *fp = proc->p_fd_table[fd];
    if (!fp) {
      spinlock_unlock_irqrestore(&proc->p_fdlock, irq);
      return -1;
    }

    for (int newfd = start; newfd < XIU_PROC_MAX_FDS; newfd++) {
      if (!proc->p_fd_table[newfd]) {
        fp_retain(fp);
        proc->p_fd_table[newfd] = fp;
        spinlock_unlock_irqrestore(&proc->p_fdlock, irq);
        return newfd;
      }
    }
    spinlock_unlock_irqrestore(&proc->p_fdlock, irq);
    return -1;
  }

  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp)
    return -1;

  i64 ret = 0;
  if (cmd == XIU_F_GETFD) {
    ret = (fp->fp_flags & FP_CLOEXEC) ? 1 : 0;
  } else if (cmd == XIU_F_SETFD) {
    irq_flags_t irq = spinlock_lock_irqsave(&fp->fp_lock);
    if (arg & 1)
      fp->fp_flags |= FP_CLOEXEC;
    else
      fp->fp_flags &= ~FP_CLOEXEC;
    spinlock_unlock_irqrestore(&fp->fp_lock, irq);
  } else if (cmd == XIU_F_GETFL) {
    ret = (fp->fp_flags & FP_NONBLOCK) ? 2048 : 0;
  } else if (cmd == XIU_F_SETFL) {
    irq_flags_t irq = spinlock_lock_irqsave(&fp->fp_lock);
    if (arg & 2048)
      fp->fp_flags |= FP_NONBLOCK;
    else
      fp->fp_flags &= ~FP_NONBLOCK;
    spinlock_unlock_irqrestore(&fp->fp_lock, irq);
  } else {
    ret = -1;
  }

  fp_release(fp);
  return ret;
}

#include <kernel/input.h>
extern size_t xiukit_hid_read_mouse(xiu_event_t *buf, size_t count);

/* ── sys_open ────────────────────────────────────────────────────────────── *
 * Opens a file by resolving the path through VFS and allocating a fileproc.
 * Returns the lowest available file descriptor, NOT a hardcoded constant.
 * ───────────────────────────────────────────────────────────────────────────
 */
static i64 sys_close(u64 fd_u, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;
  return (proc_fd_close(proc, (int)fd_u) == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_dup2(u64 oldfd_u, u64 newfd_u, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int oldfd = (int)oldfd_u;
  int newfd = (int)newfd_u;

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || oldfd < 0 || oldfd >= XIU_PROC_MAX_FDS || newfd < 0 ||
      newfd >= XIU_PROC_MAX_FDS) {
    return -1;
  }
  if (oldfd == newfd)
    return newfd;

  xiu_fileproc_t *old_fp = proc_fd_lookup(proc, oldfd);
  if (!old_fp)
    return -1;

  // close existing newfd if open
  proc_fd_close(proc, newfd);

  irq_flags_t irq = spinlock_lock_irqsave(&proc->p_fdlock);
  proc->p_fd_table[newfd] = old_fp;
  spinlock_unlock_irqrestore(&proc->p_fdlock, irq);

  return (i64)newfd;
}

static void resolve_relative_path(xiu_proc_t *proc, const char *path,
                                  char *out_buf, usize out_max) {
  (void)proc;
  extern void vfs_normalize_path(const char *in, char *out, usize cap);
  vfs_normalize_path(path, out_buf, out_max);
}

static i64 sys_open(u64 path_ptr, u64 flags, u64 mode, u64 a4, u64 a5, u64 a6) {
  (void)mode;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!path_ptr)
    return -14; // -EFAULT
  const char *path = (const char *)path_ptr;
  dprintf("[SYSCALL] open(%s)\n", path);

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  vnode_t *vp = nullptr;
  char norm_path[256];

  if (__builtin_strcmp(path, ".") == 0) {
    if (proc->p_cwd) {
      vp = proc->p_cwd;
    } else {
      vfs_lookup("/", &vp);
    }
  } else {
    resolve_relative_path(proc, path, norm_path, sizeof(norm_path));
    vfs_lookup(norm_path, &vp);

    if (!vp && (flags & 0x0200 || flags & 0x0040 || flags & 0x0100 ||
                (flags & 1) || (flags & 2))) {
      extern xiu_error_t fat32_create_file(const char *path, vnode_t **out_vp);
      fat32_create_file(norm_path, &vp);
    }
  }

  if (!vp) {
    return -1;
  }

  if (flags & 0x0400 || (flags & 0x0200 && (flags & 1 || flags & 2))) {
    if (vp->v_op && __builtin_strcmp(vp->v_op->vop_name, "fat32_file") == 0) {
      typedef struct {
        u32 start_cluster;
        u32 file_size;
      } fat32_trunc_data_t;
      fat32_trunc_data_t *nd = (fat32_trunc_data_t *)vp->v_data;
      if (nd) {
        nd->file_size = 0;
        vp->v_attr.va_size = 0;
      }
    }
  }

  u32 fp_flags = FP_READABLE;
  if (flags & 1) {
    fp_flags &= ~FP_READABLE;
    fp_flags |= FP_WRITABLE;
  }
  if (flags & 2) {
    fp_flags |= FP_READABLE | FP_WRITABLE;
  }

  // allocate fileproc backed by this vnode
  xiu_fileproc_t *fp = fp_alloc(vp, fp_flags);
  if (!fp) {
    kprintf("[sys_open] '%s': ENOMEM (fileproc pool exhausted)\n", path);
    return -1;
  }

  int fd = proc_fd_install(proc, fp);
  fp_release(fp); // proc_fd_install retained its own reference

  if (fd < 0) {
    kprintf("[sys_open] '%s': EMFILE (FDT full)\n", path);
    return -1;
  }

  dprintf("[sys_open] '%s' → fd=%d (vnode=%s)\n", path, fd, vp->v_name);
  return (i64)fd;
}

/* ── sys_read ────────────────────────────────────────────────────────────── *
 * Routes read() through the per-process FDT → vnode ops table.
 * No hardcoded fd numbers. No cli/sti.
 * ───────────────────────────────────────────────────────────────────────────
 */
static i64 sys_read(u64 fd_u, u64 buf, u64 len, u64 a4, u64 a5, u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  int fd = (int)fd_u;

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp) {
    // fallback: fd 0 without FDT entry → direct console input
    if (fd == 0) {
      char tmp[256];
      usize to_read = len < sizeof(tmp) ? len : sizeof(tmp);
      extern i64 console_read(char *dst, usize len);
      i64 n = console_read(tmp, to_read);
      if (n <= 0)
        return 0;
      if (copyout(tmp, (void *)buf, n) != XIU_SUCCESS)
        return -1;
      return n;
    }
    kprintf("[sys_read] fd=%d: EBADF\n", fd);
    return -1;
  }

  if (fp->fp_type == DTYPE_SOCKET && fp->fp_socket) {
    usize bytes_read = 0;
    xiu_error_t err =
        soreceive(fp->fp_socket, nullptr, (void *)buf, len, &bytes_read, 0);
    fp_release(fp);
    return (err == XIU_SUCCESS) ? (i64)bytes_read : -1;
  }

  vnode_t *vp = fp->fp_vnode;
  if (!vp || !vp->v_op || !vp->v_op->vop_read) {
    fp_release(fp);
    return -1;
  }

  // build a uio structure on the stack
  struct uio uio;
  uio.uio_buf = (void *)buf;
  uio.uio_resid = len;
  uio.uio_offset = fp->fp_offset;

  xiu_error_t err = vp->v_op->vop_read(vp, &uio, 0, nullptr);

  // update file position
  irq_flags_t irq = spinlock_lock_irqsave(&fp->fp_lock);
  usize bytes_read = len - uio.uio_resid;
  fp->fp_offset += bytes_read;
  spinlock_unlock_irqrestore(&fp->fp_lock, irq);

  fp_release(fp);
  return (err == XIU_SUCCESS) ? (i64)bytes_read : -1;
}

/* ── sys_pipe ────────────────────────────────────────────────────────────── *
 * Creates two file descriptors backed by an anonymous kernel FIFO pipe.
 * pipefd[0] = read fd, pipefd[1] = write fd.
 * ───────────────────────────────────────────────────────────────────────────
 */
static i64 sys_pipe(u64 pipefd_ptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!pipefd_ptr)
    return -22; // einval

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  extern xiu_error_t pipe_create(vnode_t * *read_vp_out,
                                 vnode_t * *write_vp_out);
  vnode_t *read_vp = nullptr;
  vnode_t *write_vp = nullptr;

  if (pipe_create(&read_vp, &write_vp) != XIU_SUCCESS || !read_vp ||
      !write_vp) {
    return -23; // enfile
  }

  xiu_fileproc_t *rfp = fp_alloc(read_vp, FP_READABLE);
  xiu_fileproc_t *wfp = fp_alloc(write_vp, FP_WRITABLE);
  if (!rfp || !wfp) {
    if (rfp)
      fp_release(rfp);
    if (wfp)
      fp_release(wfp);
    return -24; // emfile
  }

  int rfd = proc_fd_install(proc, rfp);
  int wfd = proc_fd_install(proc, wfp);
  fp_release(rfp);
  fp_release(wfp);
  if (rfd < 0 || wfd < 0) {
    if (rfd >= 0)
      proc_fd_close(proc, rfd);
    if (wfd >= 0)
      proc_fd_close(proc, wfd);
    return -24; // emfile
  }

  int fds[2] = {rfd, wfd};
  if (copyout(fds, (void *)pipefd_ptr, sizeof(fds)) != XIU_SUCCESS) {
    proc_fd_close(proc, rfd);
    proc_fd_close(proc, wfd);
    return -14; // efault
  }
  return 0;
}

/* ── sys_spawn ───────────────────────────────────────────────────────────── *
 * Creates and runs a new process from an ELF binary on the VFS.
 * The child inherits specified stdin/stdout vnodes.
 *
 * Signature: sys_spawn(path, argv, envp, stdin_vpath, stdout_vpath) → pid
 *   stdin_vpath  — path to slave PTY ("/dev/pts/0") for child stdin  (fd 0)
 *   stdout_vpath — path to slave PTY ("/dev/pts/0") for child stdout (fd 1)
 * ───────────────────────────────────────────────────────────────────────────
 */
extern void elf_load(void *module_ptr, struct xiu_task *out_task,
                     uintptr_t *entry_point, uintptr_t *user_stack);
extern void scheduler_add_thread(xiu_thread_t *th);
extern void thread_init_stack(xiu_thread_t *th, void *entry, void *stack);
extern xiu_proc_t *proc_launchd;

static i64 sys_spawn(u64 path_ptr, u64 argv_ptr, u64 envp_ptr,
                     u64 stdin_path_ptr, u64 stdout_path_ptr, u64 a6) {
  (void)argv_ptr;
  (void)envp_ptr;
  (void)a6;

  // copy path strings from user space
  char path[128], stdin_path[64], stdout_path[64];
  if (copyin((const void *)path_ptr, path, sizeof(path)) != XIU_SUCCESS)
    return -1;
  path[127] = '\0';

  bool has_stdio = false;
  if (stdin_path_ptr != 0 && stdout_path_ptr != 0) {
    if (copyin((const void *)stdin_path_ptr, stdin_path, sizeof(stdin_path)) !=
        XIU_SUCCESS)
      return -1;
    if (copyin((const void *)stdout_path_ptr, stdout_path,
               sizeof(stdout_path)) != XIU_SUCCESS)
      return -1;
    stdin_path[63] = stdout_path[63] = '\0';
    has_stdio = true;
    dprintf("[sys_spawn] spawning '%s' stdin='%s' stdout='%s'\n", path,
            stdin_path, stdout_path);
  } else {
    dprintf("[sys_spawn] spawning '%s' (no stdio)\n", path);
  }

  // 1. look up elf binary in vfs
  vnode_t *elf_vp = nullptr;
  if (vfs_lookup(path, &elf_vp) != XIU_SUCCESS || !elf_vp) {
    kprintf("[sys_spawn] ELF not found: %s\n", path);
    return -1;
  }

  static u8 s_elf_buf[512 * 1024];
  struct uio uio;
  uio.uio_buf = s_elf_buf;
  uio.uio_resid = sizeof(s_elf_buf);
  uio.uio_offset = 0;
  if (!elf_vp->v_op || !elf_vp->v_op->vop_read) {
    kprintf("[sys_spawn] ELF vnode has no vop_read, trying direct address\n");
  } else {
    elf_vp->v_op->vop_read(elf_vp, &uio, 0, nullptr);
  }

  void *elf_ptr = elf_vp->v_data ? elf_vp->v_data : (void *)s_elf_buf;

  // 2. create child process
  xiu_proc_t *parent = proc_launchd ? proc_launchd : proc_kernel;
  xiu_proc_t *child = nullptr;
  if (proc_create(parent, path, &child) != XIU_SUCCESS || !child)
    return -1;
  xiu_task_t *ctask = child->p_task;

  // 3. setup stdio if requested
  if (has_stdio) {
    vnode_t *svp_in = nullptr;
    vnode_t *svp_out = nullptr;
    vfs_lookup(stdin_path, &svp_in);
    vfs_lookup(stdout_path, &svp_out);
    if (!svp_in || !svp_out) {
      kprintf("[sys_spawn] PTY vnodes not found\n");
      return -1;
    }

    xiu_fileproc_t *fp_in = fp_alloc(svp_in, FP_READABLE);
    xiu_fileproc_t *fp_out = fp_alloc(svp_out, FP_WRITABLE | FP_READABLE);
    if (!fp_in || !fp_out)
      return -1;

    // force-install at fd 0 and fd 1
    irq_flags_t irq = spinlock_lock_irqsave(&child->p_fdlock);
    fp_retain(fp_in);
    child->p_fd_table[0] = fp_in; // stdin
    fp_retain(fp_out);
    child->p_fd_table[1] = fp_out; // stdout
    fp_retain(fp_out);
    child->p_fd_table[2] = fp_out; // stderr
    spinlock_unlock_irqrestore(&child->p_fdlock, irq);
    fp_release(fp_in);
    fp_release(fp_out);
  }

  // 4. load mach-o into child address space
  uintptr_t entry = 0, user_stack = 0;
  mach_load(elf_ptr, ctask, &entry, &user_stack);
  if (entry == 0) {
    kprintf("[sys_spawn] Mach-O load failed\n");
    return -1;
  }

  // 5. create and schedule child thread
  static xiu_thread_t s_spawn_threads[64];
  xiu_thread_t *th = nullptr;

  for (u32 i = 0; i < 64; i++) {
    if (s_spawn_threads[i].th_signature != XIU_THREAD_MAGIC ||
        s_spawn_threads[i].th_state == THREAD_STATE_HALTED) {
      th = &s_spawn_threads[i];
      break;
    }
  }

  if (!th) {
    kprintf("[sys_spawn] ERROR: spawn thread pool exhausted\n");
    return -1;
  }

  __builtin_memset(th, 0, sizeof(xiu_thread_t));
  th->th_signature = XIU_THREAD_MAGIC;
  th->th_task = ctask;
  th->th_state = THREAD_STATE_READY;
  th->th_priority = 0;
  th->th_context = (void *)entry;
  ctask->ta_threads = th;

  thread_init_stack(th, (void *)entry, (void *)user_stack);
  scheduler_add_thread(th);

  dprintf("[sys_spawn] spawned '%s' pid=%u entry=0x%llx\n", path, child->p_pid,
          (unsigned long long)entry);
  return (i64)child->p_pid;
}

static i64 sys_fork(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *parent_task = current_task();
  xiu_proc_t *parent = parent_task ? parent_task->ta_proc : nullptr;
  if (!parent || !g_syscall_frame)
    return -1;

  xiu_proc_t *child = nullptr;
  if (proc_create(parent, parent->p_comm, &child) != XIU_SUCCESS || !child)
    return -1;

  if (child->p_task->ta_vm_map) {
    extern void pmm_release_page(xiu_paddr_t addr);
    pmm_release_page((xiu_paddr_t)child->p_task->ta_vm_map);
  }
  child->p_task->ta_vm_map =
      (void *)pmap_clone_user_space((u64)parent_task->ta_vm_map);
  child->p_task->ta_mmap_next = parent_task->ta_mmap_next;

  irq_flags_t irq = spinlock_lock_irqsave(&parent->p_fdlock);
  irq_flags_t cirq = spinlock_lock_irqsave(&child->p_fdlock);
  for (int i = 0; i < XIU_PROC_MAX_FDS; i++) {
    xiu_fileproc_t *fp = parent->p_fd_table[i];
    if (fp) {
      fp_retain(fp);
      child->p_fd_table[i] = fp;
    }
  }
  spinlock_unlock_irqrestore(&child->p_fdlock, cirq);
  spinlock_unlock_irqrestore(&parent->p_fdlock, irq);

  // clone parent's ipc_space entries into child so child threads/processes
  // inherit Mach ports
  if (parent_task && parent_task->ta_ipc_space && child->p_task->ta_ipc_space) {
    irq_flags_t pf = spinlock_lock_irqsave(&parent_task->ta_ipc_space->is_lock);
    irq_flags_t cf =
        spinlock_lock_irqsave(&child->p_task->ta_ipc_space->is_lock);

    for (u32 i = 1; i < parent_task->ta_ipc_space->is_table_used; i++) {
      ipc_entry_t *pe = &parent_task->ta_ipc_space->is_table[i];
      if (pe->ie_object && pe->ie_bits != MACH_PORT_TYPE_NONE) {
        // Mach semantics: RECEIVE rights belong strictly to receiver space and
        // are NEVER inherited by child!
        if (pe->ie_bits & MACH_PORT_TYPE_RECEIVE) {
          continue;
        }
        if (i >= child->p_task->ta_ipc_space->is_table_used) {
          child->p_task->ta_ipc_space->is_table_used = i + 1;
        }
        ipc_entry_t *ce = &child->p_task->ta_ipc_space->is_table[i];
        ce->ie_object = pe->ie_object;
        ce->ie_bits = pe->ie_bits;
        ce->ie_urefs = pe->ie_urefs;
        ipc_port_reference(pe->ie_object);
      }
    }
    spinlock_unlock_irqrestore(&child->p_task->ta_ipc_space->is_lock, cf);
    spinlock_unlock_irqrestore(&parent_task->ta_ipc_space->is_lock, pf);
  }

  syscall_user_frame_t *frame = (syscall_user_frame_t *)g_syscall_frame;
  uptr child_rip = frame->rip;
  uptr child_rsp = frame->rsp; // user RSP from syscall frame

  static xiu_thread_t s_fork_threads[64];
  xiu_thread_t *th = nullptr;

  for (u32 i = 0; i < 64; i++) {
    if (s_fork_threads[i].th_signature != XIU_THREAD_MAGIC ||
        s_fork_threads[i].th_state == THREAD_STATE_HALTED) {
      th = &s_fork_threads[i];
      break;
    }
  }

  if (!th) {
    kprintf("[sys_fork] ERROR: fork thread pool exhausted\n");
    return -1;
  }

  __builtin_memset(th, 0, sizeof(*th));
  th->th_signature = XIU_THREAD_MAGIC;
  th->th_task = child->p_task;
  th->th_state = THREAD_STATE_READY;
  th->th_priority = current_thread() ? current_thread()->th_priority : 0;

  /* CRITICAL FIX: Copy the syscall frame into the thread structure!
   * We CANNOT just store a pointer to g_syscall_frame because that's on
   * the parent's kernel stack and will be overwritten. We must copy all
   * registers (r15, r14, r13, r12, rbx, rbp, rip, rflags, rsp) into
   * th_fork_frame so the child has a stable copy. */
  __builtin_memcpy(th->th_fork_frame, frame, sizeof(syscall_user_frame_t));
  th->th_user_frame = (void *)th->th_fork_frame; // point to our copy

  child->p_task->ta_threads = th;
  thread_init_fork_stack(th, (void *)child_rip, (void *)child_rsp);

  /* CRITICAL FIX: fork() must return 0 in child, PID in parent.
   * The child thread will resume at the same RIP as parent (after syscall),
   * but it must see RAX=0. We store this in th_fork_return_value which
   * the context switch code will load into RAX. */
  th->th_fork_return_value = 0;
  th->th_is_fork_child = 1;

  scheduler_add_thread(th);

  return child->p_pid;
}

static i64 sys_execve(u64 path_ptr, u64 argv_ptr, u64 envp_ptr, u64 a4, u64 a5,
                      u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;

  // critical: Check that we're on kernel stack
  u64 rsp;
  __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));

  if ((rsp & 0xFFFF800000000000ULL) == 0) {
    kprintf("[sys_execve] FATAL: Running on user stack! RSP=0x%llx\n",
            (unsigned long long)rsp);
    xiu_panic("sys_execve: Running on user stack");
  }

  char path[128];
  if (copyin((const void *)path_ptr, path, sizeof(path)) != XIU_SUCCESS)
    return -1;
  path[sizeof(path) - 1] = '\0';

  char arg_storage[16][128];
  char *kargv[16];
  int argc = 0;
  if (argv_ptr) {
    for (; argc < 15; argc++) {
      u64 uptr = 0;
      if (copyin((const void *)(argv_ptr + argc * sizeof(u64)), &uptr,
                 sizeof(uptr)) != XIU_SUCCESS ||
          uptr == 0)
        break;
      if (copyin((const void *)uptr, arg_storage[argc],
                 sizeof(arg_storage[argc])) != XIU_SUCCESS)
        break;
      arg_storage[argc][sizeof(arg_storage[argc]) - 1] = '\0';
      kargv[argc] = arg_storage[argc];
    }
  }
  if (argc == 0)
    kargv[argc++] = path;
  kargv[argc] = nullptr;

  char env_storage[16][128];
  char *kenvp[16];
  int envc = 0;
  if (envp_ptr) {
    for (; envc < 15; envc++) {
      u64 uptr = 0;
      if (copyin((const void *)(envp_ptr + envc * sizeof(u64)), &uptr,
                 sizeof(uptr)) != XIU_SUCCESS ||
          uptr == 0)
        break;
      if (copyin((const void *)uptr, env_storage[envc],
                 sizeof(env_storage[envc])) != XIU_SUCCESS)
        break;
      env_storage[envc][sizeof(env_storage[envc]) - 1] = '\0';
      kenvp[envc] = env_storage[envc];
    }
  }
  kenvp[envc] = nullptr;

  vnode_t *elf_vp = nullptr;
  const char *npath = normalize_path(path);
  if (vfs_lookup(npath, &elf_vp) != XIU_SUCCESS || !elf_vp) {
    return -2; // -ENOENT
  }

  void *elf_ptr = elf_vp->v_data;
  xiu_paddr_t temp_phys = (xiu_paddr_t)-1;
  usize temp_pages = 0;

  if (elf_vp->v_op &&
      __builtin_strcmp(elf_vp->v_op->vop_name, "fat32_file") == 0) {
    typedef struct {
      u32 start_cluster;
      u32 file_size;
      bool is_dir;
      char path[256];
    } fat32_node_info_t;

    fat32_node_info_t *nd = (fat32_node_info_t *)elf_vp->v_data;
    if (!nd || nd->file_size == 0) {
      kprintf("[sys_execve] ERROR: empty FAT32 file\n");
      return -1;
    }
    extern xiu_paddr_t pmm_alloc_pages(usize count);
    extern void pmm_free_contiguous(xiu_paddr_t base, usize count);
    temp_pages = (nd->file_size + 4095) / 4096;
    temp_phys = pmm_alloc_pages(temp_pages);
    if (temp_phys == (xiu_paddr_t)-1) {
      kprintf("[sys_execve] ERROR: failed to alloc %zu pages for ELF\n",
              temp_pages);
      return -1;
    }
    extern u64 g_hhdm_base;
    elf_ptr = (void *)(temp_phys + g_hhdm_base);
    u32 actual_read = 0;
    extern xiu_error_t fat32_read_file(u32 start_cluster, u32 file_size,
                                       u32 offset, void *dst, u32 len,
                                       u32 *bytes_read);
    xiu_error_t err = fat32_read_file(nd->start_cluster, nd->file_size, 0,
                                      elf_ptr, nd->file_size, &actual_read);
    if (err != XIU_SUCCESS || actual_read == 0) {
      kprintf("[sys_execve] ERROR: failed to read ELF from disk (err=%d)\n",
              err);
      pmm_free_contiguous(temp_phys, temp_pages);
      return -1;
    }
  }

  if (!elf_ptr) {
    kprintf("[sys_execve] ERROR: elf_ptr is NULL\n");
    return -1;
  }

  xiu_task_t *task = current_task();
  if (!task) {
    if (temp_pages > 0) {
      extern void pmm_free_contiguous(xiu_paddr_t base, usize count);
      pmm_free_contiguous(temp_phys, temp_pages);
    }
    return -1;
  }

  if (!task->ta_vm_map) {
    if (temp_pages > 0) {
      extern void pmm_free_contiguous(xiu_paddr_t base, usize count);
      pmm_free_contiguous(temp_phys, temp_pages);
    }
    return -1;
  }

  // clean old user address space and allocate fresh PML4 for the new binary
  extern void pmap_destroy_user_space(u64 pml4_phys);
  extern xiu_paddr_t pmm_alloc_page(void);
#define get_table_ptr_exec(p) ((u64 *)((p) + g_hhdm_base))

  u64 old_pml4 = (u64)task->ta_vm_map;
  u64 new_pml4 = pmm_alloc_page();
  __builtin_memset(get_table_ptr_exec(new_pml4), 0, 4096);

  // copy kernel mappings
  u64 *new_pml4_v = get_table_ptr_exec(new_pml4);
  u64 *old_pml4_v = get_table_ptr_exec(old_pml4);
  for (int i = 256; i < 512; i++) {
    new_pml4_v[i] = old_pml4_v[i];
  }

  pmap_destroy_user_space(old_pml4);

  // assign fresh PML4 to task and flush TLB
  task->ta_vm_map = (void *)new_pml4;
  __asm__ volatile("mov %0, %%cr3" ::"r"(new_pml4) : "memory");

  // reset mmap base for the new process
  task->ta_mmap_next = 0;

  uptr entry = 0, user_stack = 0;
  mach_load_args(elf_ptr, task, &entry, &user_stack, path, kargv, kenvp);

  if (temp_pages > 0) {
    extern void pmm_free_contiguous(xiu_paddr_t base, usize count);
    pmm_free_contiguous(temp_phys, temp_pages);
  }
  if (!entry) {
    return -1;
  }

  if (task->ta_proc) {
    __builtin_memset(task->ta_proc->p_comm, 0, XIU_PROC_NAME_MAX);
    __builtin_strncpy(task->ta_proc->p_comm, path, XIU_PROC_NAME_MAX - 1);
  }

  task_switch_to_user_frame(entry, user_stack, nullptr, 0);
  XIU_UNREACHABLE();
}

extern u64 g_fb_phys_addr;
extern u64 pmap_map_user_page(u64 pml4, u64 vaddr, u64 paddr, u32 flags);

#define PAGE_WRITE (1ULL << 1)
#define PAGE_USER (1ULL << 2)

static i64 sys_mmap(u64 addr, u64 len, u64 prot, u64 flags, u64 fd,
                    u64 offset) {
  (void)flags;
  (void)offset;
  xiu_task_t *task = current_task();
  dprintf("[SYSCALL] mmap(task=%d, addr=%p, len=%zu, fd=%llu)\n",
          task ? task->ta_id : -1, (void *)addr, (usize)len,
          (unsigned long long)fd);

  /* Classify the mapping type using the per-process FDT.
   * fd == -1 (arrives as 0xFFFFFFFF) → anonymous mapping.
   * Any other fd → look up vnode; if it's /dev/fb0 (VCHR with mmap op),
   * treat as framebuffer mapping. This eliminates the hardcoded fd==3. */
  bool is_anon = (fd == (u64)-1 || (fd & 0xFFFFFFFF) == 0xFFFFFFFF);
  bool is_fb = false;
  vnode_t *file_vp = nullptr;

  if (!is_anon) {
    // look up the fd in the calling process's file descriptor table
    xiu_task_t *calling_task = current_task();
    xiu_proc_t *calling_proc = calling_task ? calling_task->ta_proc : nullptr;
    if (calling_proc) {
      xiu_fileproc_t *mmap_fp = proc_fd_lookup(calling_proc, (int)fd);
      if (mmap_fp && mmap_fp->fp_vnode) {
        file_vp = mmap_fp->fp_vnode;
        // /dev/fb0 has VCHR type and provides vop_mmap
        if (file_vp->v_type == VCHR && file_vp->v_op &&
            file_vp->v_op->vop_mmap != nullptr) {
          is_fb = true;
        }
      }
      if (mmap_fp)
        fp_release(mmap_fp);
    }
    if (!is_fb && !file_vp)
      return -1; // unsupported fd type for mmap
  }

  if (!task || !task->ta_vm_map)
    return -1;

  u64 pml4 = (u64)task->ta_vm_map;

  // pick virtual base — automatic anonymous mappings must not overlap.
  u64 vaddr = addr;
  if (vaddr == 0) {
    if (is_fb) {
      vaddr = 0x80000000ULL; // framebuffer region
    } else {
      if (task->ta_mmap_next == 0) {
        task->ta_mmap_next = 0x90000000ULL;
      }
      vaddr = task->ta_mmap_next;
      task->ta_mmap_next = (vaddr + len + 0xFFFULL) & ~0xFFFULL;
    }
  } else if (!is_fb && is_anon) {
    /* ponytail: only bump ta_mmap_next for standard anon memory (0x90000000
     * region), not shared surfaces (0xA0000000 region) */
    if (vaddr >= 0x90000000ULL && vaddr < 0xA0000000ULL) {
      u64 end_addr = (vaddr + len + 0xFFFULL) & ~0xFFFULL;
      if (task->ta_mmap_next < end_addr) {
        task->ta_mmap_next = end_addr;
      }
    }
  }

  u32 pg_flags = PAGE_USER | PAGE_WRITE;
  if (prot & 4)
    pg_flags |= 0;

  extern xiu_paddr_t pmm_alloc_page(void);
  extern u64 g_hhdm_base;

  typedef struct {
    vnode_t *vp;
    u32 page_count;
    xiu_paddr_t pages[2048];
  } xiu_shm_entry_t;
  static xiu_shm_entry_t s_shm_entries[64];
  static spinlock_t s_shm_lock = {0};

  xiu_shm_entry_t *shm = nullptr;
  if (file_vp && !is_fb) {
    irq_flags_t sf = spinlock_lock_irqsave(&s_shm_lock);
    for (int i = 0; i < 64; i++) {
      if (s_shm_entries[i].vp == file_vp) {
        shm = &s_shm_entries[i];
        break;
      }
    }
    if (!shm) {
      for (int i = 0; i < 64; i++) {
        if (s_shm_entries[i].vp == nullptr) {
          shm = &s_shm_entries[i];
          shm->vp = file_vp;
          shm->page_count = 0;
          __builtin_memset(shm->pages, 0, sizeof(shm->pages));
          break;
        }
      }
    }
    spinlock_unlock_irqrestore(&s_shm_lock, sf);
  }

  static u64 s_surface_phys[64][2000];

  if (is_fb) {
    extern void console_fb_set_active(bool active);
    console_fb_set_active(false);
  }

  for (u64 off = 0; off < len; off += 4096) {
    u64 paddr = 0;
    bool mapped_special = false;

    if (is_fb) {
      paddr = g_fb_phys_addr + off;
      mapped_special = true;
    } else if (shm) {
      u64 page_idx = off / 4096;
      if (page_idx < 2048) {
        if (shm->pages[page_idx] == 0) {
          shm->pages[page_idx] = pmm_alloc_page();
          if (g_hhdm_base && shm->pages[page_idx] != (u64)-1) {
            __builtin_memset((void *)(g_hhdm_base + shm->pages[page_idx]), 0,
                             4096);
          }
        }
        paddr = shm->pages[page_idx];
        mapped_special = true;
      }
    } else if (vaddr >= 0xA0000000ULL && vaddr < 0xB0000000ULL) {
      // shared window surface
      u64 wid = (vaddr - 0xA0000000ULL) / 0x800000ULL;
      u64 page_idx = off / 4096;
      if (wid < 64 && page_idx < 2000) {
        if (s_surface_phys[wid][page_idx] == 0) {
          s_surface_phys[wid][page_idx] = pmm_alloc_page();
          if (g_hhdm_base && s_surface_phys[wid][page_idx] != (u64)-1) {
            __builtin_memset(
                (void *)(g_hhdm_base + s_surface_phys[wid][page_idx]), 0, 4096);
          }
        }
        paddr = s_surface_phys[wid][page_idx];
        mapped_special = true;
      }
    }

    if (!mapped_special) {
      // normal anonymous allocation
      paddr = pmm_alloc_page();
      if (paddr == (u64)-1) {
        kprintf("[mmap] ERROR: pmm_alloc_page() returned NULL at offset %llu\n",
                (unsigned long long)off);
        return -1;
      }
      // zero the physical page via HHDM so shadow buffer starts clean
      if (g_hhdm_base) {
        u64 *kva = (u64 *)(g_hhdm_base + paddr);
        for (usize w = 0; w < 4096 / 8; w++)
          kva[w] = 0;
      }
    }
    pmap_map_user_page(pml4, vaddr + off, paddr, pg_flags);
  }

  if (is_fb)
    dprintf("[mmap] FB mapped:     phys=0x%llx → user=0x%llx\n",
            (unsigned long long)g_fb_phys_addr, (unsigned long long)vaddr);
  else
    dprintf("[mmap] Shadow mapped: anon         → user=0x%llx (%llu pages)\n",
            (unsigned long long)vaddr, (unsigned long long)(len / 4096));

  return (i64)vaddr;
}

static i64 sys_munmap(u64 addr, u64 len, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!addr || !len)
    return -22; // einval

  xiu_task_t *task = current_task();
  if (!task || !task->ta_vm_map)
    return -1;

  extern void pmap_unmap_user_range(u64 pml4_phys, u64 vaddr, usize len);
  pmap_unmap_user_range((u64)task->ta_vm_map, addr, (usize)len);
  return 0;
}

extern xiu_error_t ipc_port_alloc(ipc_space_t *space,
                                  mach_port_name_t *name_out,
                                  const char *label);
extern xiu_error_t ipc_kmsg_copyin(ipc_kmsg_t *kmsg, u64 user_header,
                                   ipc_space_t *space);
extern xiu_error_t ipc_kmsg_copyout(ipc_kmsg_t *kmsg, u64 user_buf,
                                    u32 buf_size, ipc_space_t *space);
extern xiu_error_t ipc_mqueue_send(struct ipc_port *port, ipc_kmsg_t *kmsg,
                                   u32 timeout);
extern xiu_error_t ipc_mqueue_receive(struct ipc_port *port,
                                      ipc_kmsg_t **kmsg_out, u32 timeout);
extern ipc_kmsg_t *ipc_kmsg_alloc(u32 size);
extern void ipc_kmsg_free(ipc_kmsg_t *kmsg);
extern void ipc_port_unlock(struct ipc_port *port);
extern void ipc_port_reference(struct ipc_port *port);
extern mach_port_name_t space_alloc_name(ipc_space_t *space);
extern xiu_error_t mach_register_service(const char *name,
                                         struct ipc_port *port);
extern struct ipc_port *mach_lookup_service(const char *name);
extern xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);
extern xiu_error_t copyin(const void *uaddr, void *kaddr, usize len);

static i64 sys_mach_msg(u64 msg_ptr, u64 option, u64 send_sz, u64 rcv_sz,
                        u64 rcv_name, u64 timeout) {
  xiu_task_t *task = current_task();
  if (!task)
    return -1;

  u32 cur_pid = task->ta_proc ? task->ta_proc->p_pid : 0;

  if (option & 1) {
    mach_msg_header_t user_hdr;
    if (copyin((const void *)msg_ptr, &user_hdr, sizeof(user_hdr)) !=
        XIU_SUCCESS) {
      kprintf("[IPC-ERR] sys_mach_msg SEND copyin header failed for "
              "msg_ptr=0x%llx (pid=%u)\n",
              msg_ptr, cur_pid);
      return -1;
    }

    ipc_kmsg_t *kmsg = ipc_kmsg_alloc(send_sz);
    if (ipc_kmsg_copyin(kmsg, msg_ptr, task->ta_ipc_space) != XIU_SUCCESS) {
      kprintf("[IPC-ERR] sys_mach_msg SEND ipc_kmsg_copyin failed for pid=%u "
              "msg_ptr=0x%llx send_sz=%llu\n",
              cur_pid, msg_ptr, send_sz);
      ipc_kmsg_free(kmsg);
      return -1;
    }

    struct ipc_port *port = kmsg->ikm_remote_port;
    kprintf("[IPC] sys_mach_msg SEND: pid=%u to_port=%p msgh_id=%u\n", cur_pid,
            (void *)port, ((mach_msg_header_t *)kmsg->ikm_header)->msgh_id);
    xiu_error_t err = ipc_mqueue_send(port, kmsg, (u32)timeout);

    if (err != XIU_SUCCESS) {
      if (err != XIU_ERR_PORT_FULL)
        kprintf("[IPC] Send failed for pid=%u with error %d\n", cur_pid, err);
      ipc_kmsg_free(kmsg);
      return -1;
    }
  }

  if (option & 2) {
    struct ipc_port *port = ipc_port_lookup(
        task->ta_ipc_space, (mach_port_name_t)rcv_name, MACH_PORT_TYPE_RECEIVE);
    if (!port) {
      kprintf("[IPC] RCV port lookup failed for rcv_name=0x%llx (pid=%u)\n",
              (unsigned long long)rcv_name, cur_pid);
      return -1;
    }

    ipc_kmsg_t *kmsg = nullptr;
    xiu_error_t err = ipc_mqueue_receive(port, &kmsg, (u32)timeout);

    if (err != XIU_SUCCESS) {
      if (timeout != 0) {
        kprintf(
            "[IPC] sys_mach_msg RCV: pid=%u port=%p timeout_ms=%llu err=%d\n",
            cur_pid, (void *)port, timeout, err);
      }
      return -1;
    }

    kprintf("[IPC] sys_mach_msg RCV: pid=%u port=%p DEQUEUED msgh_id=%u\n",
            cur_pid, (void *)port,
            ((mach_msg_header_t *)kmsg->ikm_header)->msgh_id);

    if (ipc_kmsg_copyout(kmsg, msg_ptr, (u32)rcv_sz, task->ta_ipc_space) !=
        XIU_SUCCESS) {
      kprintf(
          "[IPC-ERR] sys_mach_msg RCV copyout failed for pid=%u rcv_sz=%llu\n",
          cur_pid, rcv_sz);
      ipc_kmsg_free(kmsg);
      return -1;
    }
    ipc_kmsg_free(kmsg);
  }

  return 0;
}

static i64 sys_mach_port_allocate(u64 space_ptr, u64 right, u64 name_out_ptr,
                                  u64 a4, u64 a5, u64 a6) {
  (void)space_ptr;
  (void)right;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  if (!task)
    return -1;

  mach_port_name_t name;
  if (ipc_port_alloc(task->ta_ipc_space, &name, "user_port") != XIU_SUCCESS)
    return -1;

  if (copyout(&name, (void *)name_out_ptr, sizeof(name)) != XIU_SUCCESS) {
    // ideally we would deallocate the port here on copyout failure
    return -1;
  }
  return 0;
}

static i64 sys_mach_register_service(u64 name_ptr, u64 port_name, u64 a3,
                                     u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  if (!task)
    return -1;

  struct ipc_port *port = ipc_port_lookup(
      task->ta_ipc_space, (mach_port_name_t)port_name, MACH_PORT_TYPE_RECEIVE);
  if (!port) {
    port = ipc_port_lookup(task->ta_ipc_space, (mach_port_name_t)port_name,
                           MACH_PORT_TYPE_SEND);
  }
  if (!port) {
    kprintf(
        "[IPC] sys_mach_register_service: port_lookup failed for port 0x%llx\n",
        (unsigned long long)port_name);
    return -1;
  }

  // need to safely copy the name string from user space
  char safe_name[64];
  if (copyin((const void *)name_ptr, safe_name, sizeof(safe_name)) !=
      XIU_SUCCESS) {
    // if string is shorter than 64 bytes or page fault occurs
    /* Note: copyin needs to handle short strings gracefully,
       for now we copy up to 64 bytes which is safe since we just want a
       null-terminated string */
  }
  safe_name[63] = '\0'; // ensure null-termination

  xiu_error_t err = mach_register_service(safe_name, port);
  kprintf("[IPC] sys_mach_register_service: %s port=%p (err=%d)\n", safe_name,
          (void *)port, err);

  ipc_port_unlock(port);
  return (err == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_mach_lookup_service(u64 name_ptr, u64 name_out_ptr, u64 a3,
                                   u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  if (!task)
    return -1;

  char safe_name[64];
  if (copyin((const void *)name_ptr, safe_name, sizeof(safe_name)) !=
      XIU_SUCCESS) {
    // best effort for stage 5
  }
  safe_name[63] = '\0';

  struct ipc_port *port = mach_lookup_service(safe_name);
  if (!port)
    return -1;

  // we need to insert a send right for this port into the caller's space
  mach_port_name_t name;
  // simplified for Stage 5: allocate a name and give it a send right
  name = space_alloc_name(task->ta_ipc_space);
  ipc_entry_t *entry = &task->ta_ipc_space->is_table[name];
  entry->ie_object = port;
  entry->ie_bits = MACH_PORT_TYPE_SEND;
  entry->ie_urefs = 1;

  ipc_port_reference(port);

  if (copyout(&name, (void *)name_out_ptr, sizeof(name)) != XIU_SUCCESS) {
    return -1;
  }
  kprintf("[IPC] sys_mach_lookup_service: %s -> port_name=0x%x (task %d)\n",
          safe_name, name, task->ta_id);
  return 0;
}

static i64 sys_mach_port_deallocate(u64 space_ptr, u64 name, u64 a3, u64 a4,
                                    u64 a5, u64 a6) {
  (void)space_ptr;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  if (!task || !task->ta_ipc_space)
    return -1;
  extern xiu_error_t mach_port_deallocate_kernel(ipc_space_t * space,
                                                 mach_port_name_t name);
  return (mach_port_deallocate_kernel(task->ta_ipc_space,
                                      (mach_port_name_t)name) == XIU_SUCCESS)
             ? 0
             : -1;
}

static i64 sys_mach_port_type(u64 space_ptr, u64 name, u64 ptype_out, u64 a4,
                              u64 a5, u64 a6) {
  (void)space_ptr;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  if (!task || !task->ta_ipc_space)
    return -1;
  mach_port_type_t ptype = 0;
  extern xiu_error_t mach_port_type_kernel(
      ipc_space_t * space, mach_port_name_t name, mach_port_type_t * ptype);
  if (mach_port_type_kernel(task->ta_ipc_space, (mach_port_name_t)name,
                            &ptype) != XIU_SUCCESS)
    return -1;
  if (copyout(&ptype, (void *)ptype_out, sizeof(ptype)) != XIU_SUCCESS)
    return -1;
  return 0;
}

static i64 sys_task_self(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  if (!task || !task->ta_ipc_space)
    return MACH_PORT_NAME_NULL;
  return (i64)task->ta_task_port;
}

static i64 sys_ioctl(u64 fd_u, u64 cmd, u64 arg, u64 a4, u64 a5, u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  int fd = (int)fd_u;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp)
    return -1;

  vnode_t *vp = fp->fp_vnode;
  if (!vp || !vp->v_op || !vp->v_op->vop_ioctl) {
    fp_release(fp);
    return -1;
  }

  xiu_error_t err = vp->v_op->vop_ioctl(vp, cmd, (xiu_vaddr_t)arg, nullptr);
  fp_release(fp);
  return (err == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_stat(u64 path, u64 statbuf, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  char kpath[256];
  if (copyin((const void *)path, kpath, sizeof(kpath)) != XIU_SUCCESS)
    return -1;
  kpath[sizeof(kpath) - 1] = '\0';

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  char norm_path[256];
  resolve_relative_path(proc, kpath, norm_path, sizeof(norm_path));

  vnode_t *vp = nullptr;
  if (vfs_lookup(norm_path, &vp) != XIU_SUCCESS || !vp)
    return -1;

  xiu_user_stat_t st;
  __builtin_memset(&st, 0, sizeof(st));
  st.st_dev = 1;
  st.st_mode = vnode_mode(vp);
  st.st_nlink = (vp->v_type == VDIR) ? 2 : 1;
  st.st_ino = (u64)((uptr)vp & 0xFFFFFFFFu);
  st.st_uid = 0;
  st.st_gid = 0;
  st.st_rdev = 0;
  st.st_size = (i64)vp->v_attr.va_size;
  st.st_blksize = 4096;
  st.st_blocks = (st.st_size + 511) / 512;
  return (copyout(&st, (void *)statbuf, sizeof(st)) == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_getcwd(u64 buf, u64 size, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!buf || size < 2)
    return -1;

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || !proc->p_cwd) {
    const char cwd[] = "/";
    return (copyout(cwd, (void *)buf, sizeof(cwd)) == XIU_SUCCESS) ? 0 : -1;
  }

  extern const char *vfs_path_for_vnode(vnode_t * vp);
  const char *path = vfs_path_for_vnode(proc->p_cwd);
  if (!path || path[0] == '\0') {
    if (proc->p_cwd->v_op &&
        __builtin_strcmp(proc->p_cwd->v_op->vop_name, "fat32_dir") == 0) {
      typedef struct {
        u32 start_cluster;
        u32 file_size;
        bool is_dir;
        char path[256];
      } fat32_path_info_t;
      fat32_path_info_t *nd = (fat32_path_info_t *)proc->p_cwd->v_data;
      if (nd && nd->path[0])
        path = nd->path;
    }
    if (!path || path[0] == '\0') {
      if (proc->p_cwd->v_name[0] != '\0') {
        path = proc->p_cwd->v_name;
      } else {
        path = "/";
      }
    }
  }

  usize len = __builtin_strlen(path) + 1;
  if (len > size)
    return -34; // erange
  return (copyout(path, (void *)buf, len) == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_mkdir(u64 path_ptr, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)mode;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!path_ptr)
    return -1;
  const char *path = (const char *)path_ptr;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;

  char full_path[256];
  resolve_relative_path(proc, path, full_path, sizeof(full_path));

  vnode_t *out_vp = nullptr;
  extern xiu_error_t fat32_create_dir(const char *path, vnode_t **out_vp);
  xiu_error_t err = fat32_create_dir(full_path, &out_vp);
  return (err == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_rmdir(u64 path_ptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!path_ptr)
    return -1;
  const char *path = (const char *)path_ptr;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;

  char full_path[256];
  resolve_relative_path(proc, path, full_path, sizeof(full_path));

  extern xiu_error_t fat32_unlink_file(const char *path);
  xiu_error_t err = fat32_unlink_file(full_path);
  return (err == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_unlink(u64 path_ptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  return sys_rmdir(path_ptr, a2, a3, a4, a5, a6);
}

static i64 sys_getdents(u64 fd, u64 buf, u64 count, u64 a4, u64 a5, u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  if (count < sizeof(xiu_user_dirent_t))
    return -1;

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc) {
    kprintf("[sys_getdents] ERROR: no proc\n");
    return -1;
  }

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd);
  if (!fp) {
    kprintf("[sys_getdents] ERROR: fd %d not found\n", (int)fd);
    return -1;
  }

  vnode_t *child = nullptr;
  char name[256];
  xiu_error_t err = vfs_readdir_flat(fp->fp_vnode, (u32)fp->fp_offset, name,
                                     sizeof(name), &child);
  if (err != XIU_SUCCESS) {
    fp_release(fp);
    return 0;
  }

  dprintf("[sys_getdents] fd=%d offset=%llu name='%s' buf=0x%llx\n", (int)fd,
          (unsigned long long)fp->fp_offset, name, (unsigned long long)buf);

  xiu_user_dirent_t de;
  __builtin_memset(&de, 0, sizeof(de));
  de.d_ino = (u64)(uptr)child;
  de.d_seekoff = fp->fp_offset + 1;
  de.d_reclen = sizeof(de);
  de.d_namlen = (u16)__builtin_strlen(name);
  de.d_type = vnode_dtype(child);
  __builtin_strncpy(de.d_name, name, sizeof(de.d_name) - 1);

  irq_flags_t irq = spinlock_lock_irqsave(&fp->fp_lock);
  fp->fp_offset++;
  spinlock_unlock_irqrestore(&fp->fp_lock, irq);
  fp_release(fp);

  xiu_error_t copy_err = copyout(&de, (void *)buf, sizeof(de));
  if (copy_err != XIU_SUCCESS) {
    kprintf("[sys_getdents] ERROR: copyout failed, err=%d\n", copy_err);
    return -1;
  }

  return (i64)sizeof(de);
}

static i64 sys_wait4(u64 pid_u, u64 status_ptr, u64 options, u64 rusage, u64 a5,
                     u64 a6) {
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  extern void scheduler_yield(void);
  extern int proc_has_children(xiu_proc_t * parent);
  xiu_pid_t pid = (xiu_pid_t)pid_u;

  /* Check if this process has ANY children at all (not just exited ones).
   * If no children exist, return -ECHILD immediately instead of blocking
   * forever. */
  if (!proc_has_children(proc)) {
    return -10; // ECHILD
  }

  for (;;) {
    xiu_proc_t *child = proc_find_waitable_child(proc, pid);
    if (child) {
      int status = (int)(child->p_exit_code << 8);
      xiu_pid_t child_pid = child->p_pid;

      extern void proc_reap(xiu_proc_t * proc);
      proc_reap(child);

      if (status_ptr)
        copyout(&status, (void *)status_ptr, sizeof(status));
      if (rusage) {
        u8 zero_ru[144];
        __builtin_memset(zero_ru, 0, sizeof(zero_ru));
        copyout(zero_ru, (void *)rusage, sizeof(zero_ru));
      }

      return child_pid;
    }

    /* WNOHANG (0x01): return 0 immediately if children exist but none have
     * exited */
    if (options & 0x01) {
      return 0;
    }

    /* Re-check if we still have children before yielding.
     * A child might have exited and been reaped by another thread. */
    if (!proc_has_children(proc)) {
      return -10; // ECHILD
    }

    scheduler_yield();
  }
}

static i64 sys_yield(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  extern void scheduler_yield(void);
  scheduler_yield();
  return 0;
}

struct xiu_timespec {
  i64 tv_sec;
  i64 tv_nsec;
};

static i64 sys_nanosleep(u64 req_ptr, u64 rem_ptr, u64 a3, u64 a4, u64 a5,
                         u64 a6) {
  (void)rem_ptr;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!req_ptr)
    return -14; // EFAULT

  struct xiu_timespec ts;
  extern xiu_error_t copyin(const void *uaddr, void *kaddr, usize len);
  if (copyin((const void *)req_ptr, &ts, sizeof(ts)) != XIU_SUCCESS) {
    return -14;
  }

  if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000ULL) {
    return -22; // EINVAL
  }

  extern u64 timer_get_uptime_ms(void);
  extern void scheduler_yield(void);

  u64 target_ms = (u64)ts.tv_sec * 1000 + (u64)ts.tv_nsec / 1000000ULL;
  if (target_ms == 0 && (ts.tv_sec > 0 || ts.tv_nsec > 0)) {
    target_ms = 1;
  }

  u64 start_ms = timer_get_uptime_ms();
  while ((timer_get_uptime_ms() - start_ms) < target_ms) {
    scheduler_yield();
  }

  return 0;
}

// sys_sysinfo
typedef struct {
  u64 total_memory;
  u64 free_memory;
  u32 cpu_count;
  u32 uptime_seconds;
  char os_name[32];
  char os_version[32];
  char kernel_name[32];
  char architecture[16];
  char hostname[64];
} xiu_sysinfo_t;

extern usize pmm_total_pages(void);
extern usize pmm_free_pages(void);
extern u32 g_cpu_count; // from kernel/xiu_kernel_main.c

static i64 sys_sysinfo(u64 buf_ptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  xiu_sysinfo_t info;
  __builtin_memset(&info, 0, sizeof(info));

  // get memory info from PMM
  usize total_pages = pmm_total_pages();
  usize free_pages = pmm_free_pages();
  info.total_memory = total_pages * XIU_PAGE_SIZE;
  info.free_memory = free_pages * XIU_PAGE_SIZE;

  // cpu count - real data from Limine SMP
  extern u32 smp_get_active_cpus(void);
  info.cpu_count = smp_get_active_cpus();

  // real uptime from PIT/LAPIC timer ticks
  extern u64 timer_get_uptime_seconds(void);
  info.uptime_seconds = (u32)timer_get_uptime_seconds();

  // os information
  const char *os_name = "XIU Operating System";
  const char *os_version = "0.1.0";
  const char *kernel_name = "XIU Mach-BSD Hybrid";
  const char *architecture = "x86_64";
  const char *hostname = "xiu-system";

  // copy strings safely
  for (int i = 0; i < 31 && os_name[i]; i++)
    info.os_name[i] = os_name[i];
  for (int i = 0; i < 31 && os_version[i]; i++)
    info.os_version[i] = os_version[i];
  for (int i = 0; i < 31 && kernel_name[i]; i++)
    info.kernel_name[i] = kernel_name[i];
  for (int i = 0; i < 15 && architecture[i]; i++)
    info.architecture[i] = architecture[i];
  for (int i = 0; i < 63 && hostname[i]; i++)
    info.hostname[i] = hostname[i];

  // copy to user space
  xiu_error_t err = copyout(&info, (void *)buf_ptr, sizeof(info));
  if (err != XIU_SUCCESS) {
    kprintf("[sys_sysinfo] copyout failed\n");
    return -1;
  }

  return 0;
}

// sys_proclist
typedef struct {
  u32 pid;
  u32 ppid;
  u32 state;
  u32 thread_count;
  char name[32];
} xiu_procinfo_t;

#define PROC_POOL_SIZE 64
extern xiu_proc_t *proc_kernel;

static i64 sys_proclist(u64 buf_ptr, u64 max_count, u64 a3, u64 a4, u64 a5,
                        u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if (max_count == 0 || max_count > PROC_POOL_SIZE) {
    max_count = PROC_POOL_SIZE;
  }

  xiu_procinfo_t *user_buf = (xiu_procinfo_t *)buf_ptr;
  u32 count = 0;

  // iterate through process pool
  extern xiu_proc_t s_proc_pool[];
  extern xiu_proc_t s_kernel_proc_obj;

  // add kernel process first
  if (count < max_count && proc_kernel) {
    xiu_procinfo_t info;
    __builtin_memset(&info, 0, sizeof(info));
    info.pid = proc_kernel->p_pid;
    info.ppid = 0;
    info.state = proc_kernel->p_state;
    info.thread_count =
        proc_kernel->p_task ? proc_kernel->p_task->ta_thread_count : 0;

    // safely copy name
    const char *name = proc_kernel->p_comm;
    for (int i = 0; i < 31 && name[i] != '\0'; i++) {
      info.name[i] = name[i];
    }
    info.name[31] = '\0';

    xiu_error_t err = copyout(&info, &user_buf[count], sizeof(info));
    if (err == XIU_SUCCESS)
      count++;
  }

  // add user processes
  for (u32 i = 0; i < PROC_POOL_SIZE && count < max_count; i++) {
    xiu_proc_t *p = &s_proc_pool[i];
    if (p->p_signature != XIU_PROC_MAGIC)
      continue;

    xiu_procinfo_t info;
    __builtin_memset(&info, 0, sizeof(info));
    info.pid = p->p_pid;
    info.ppid = p->p_ppid;
    info.state = p->p_state;
    info.thread_count = p->p_task ? p->p_task->ta_thread_count : 0;

    // safely copy name
    const char *name = p->p_comm;
    for (int j = 0; j < 31 && name[j] != '\0'; j++) {
      info.name[j] = name[j];
    }
    info.name[31] = '\0';

    xiu_error_t err = copyout(&info, &user_buf[count], sizeof(info));
    if (err == XIU_SUCCESS)
      count++;
  }

  return (i64)count;
}

static i64 sys_kill(u64 pid_u, u64 sig_u, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_pid_t pid = (xiu_pid_t)pid_u;
  int sig = (int)sig_u;
  if (sig <= 0 || sig >= 32)
    return -22; // einval

  xiu_proc_t *target = proc_find_by_pid(pid);
  if (!target)
    return -3; // esrch

  extern xiu_error_t proc_signal(xiu_proc_t * proc, int sig);
  proc_signal(target, sig);
  return 0;
}

struct xiu_sigaction_layout {
  void (*sa_handler)(int);
  u32 sa_mask;
  int sa_flags;
};

static i64 sys_sigaction(u64 signum_u, u64 act_ptr, u64 oldact_ptr, u64 a4,
                         u64 a5, u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  int sig = (int)signum_u;
  if (sig <= 0 || sig >= 32 || sig == 9 || sig == 19) {
    return -22;
  }

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  irq_flags_t irq = spinlock_lock_irqsave(&proc->p_lock);

  if (oldact_ptr) {
    struct xiu_sigaction_layout old;
    old.sa_handler = (void (*)(int))proc->p_sigacts[sig];
    old.sa_flags = (int)proc->p_sigact_flags[sig];
    old.sa_mask = proc->p_sigact_mask[sig];
    spinlock_unlock_irqrestore(&proc->p_lock, irq);
    copyout(&old, (void *)oldact_ptr, sizeof(old));
    irq = spinlock_lock_irqsave(&proc->p_lock);
  }

  if (act_ptr) {
    struct xiu_sigaction_layout act;
    spinlock_unlock_irqrestore(&proc->p_lock, irq);
    if (copyin((const void *)act_ptr, &act, sizeof(act)) == XIU_SUCCESS) {
      irq = spinlock_lock_irqsave(&proc->p_lock);
      proc->p_sigacts[sig] = (u64)act.sa_handler;
      proc->p_sigact_flags[sig] = (u32)act.sa_flags;
      proc->p_sigact_mask[sig] = act.sa_mask;
      spinlock_unlock_irqrestore(&proc->p_lock, irq);
    }
    return 0;
  }

  spinlock_unlock_irqrestore(&proc->p_lock, irq);
  return 0;
}

static i64 sys_sigprocmask(u64 how_u, u64 set_ptr, u64 oldset_ptr, u64 a4,
                           u64 a5, u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  irq_flags_t irq = spinlock_lock_irqsave(&proc->p_lock);
  if (oldset_ptr) {
    u32 old = proc->p_sigmask;
    spinlock_unlock_irqrestore(&proc->p_lock, irq);
    copyout(&old, (void *)oldset_ptr, sizeof(u32));
    irq = spinlock_lock_irqsave(&proc->p_lock);
  }

  if (set_ptr) {
    u32 set = 0;
    spinlock_unlock_irqrestore(&proc->p_lock, irq);
    if (copyin((const void *)set_ptr, &set, sizeof(u32)) == XIU_SUCCESS) {
      set &= ~((1U << 9) | (1U << 19)); // cannot mask SIGKILL or SIGSTOP
      irq = spinlock_lock_irqsave(&proc->p_lock);
      int how = (int)how_u;
      if (how == 0)
        proc->p_sigmask |= set;
      else if (how == 1)
        proc->p_sigmask &= ~set;
      else if (how == 2)
        proc->p_sigmask = set;
      spinlock_unlock_irqrestore(&proc->p_lock, irq);
    }
    return 0;
  }

  spinlock_unlock_irqrestore(&proc->p_lock, irq);
  return 0;
}

// socket system calls
static i64 sys_socket(u64 dom, u64 type, u64 proto, u64 a4, u64 a5, u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  socket_t *so = nullptr;
  xiu_error_t err = socreate((int)dom, &so, (int)type, (int)proto);
  if (err != XIU_SUCCESS || !so)
    return -1;

  xiu_fileproc_t *fp = fp_alloc_socket(so, FP_READABLE | FP_WRITABLE);
  if (!fp) {
    soclose(so);
    return -1;
  }

  i32 fd = proc_fd_install(proc, fp);
  if (fd < 0) {
    fp_release(fp);
    return -1;
  }
  return fd;
}

static i64 sys_bind(u64 fd_u, u64 addr_ptr, u64 addrlen, u64 a4, u64 a5,
                    u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp)
    return -1;
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -1; // enotsock
  }

  struct sockaddr_in sin;
  if (addrlen < sizeof(struct sockaddr_in) ||
      copyin((const void *)addr_ptr, &sin, sizeof(sin)) != XIU_SUCCESS) {
    fp_release(fp);
    return -1;
  }

  xiu_error_t err = sobind(fp->fp_socket, (struct sockaddr *)&sin);
  fp_release(fp);
  return (err == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_connect(u64 fd_u, u64 addr_ptr, u64 addrlen, u64 a4, u64 a5,
                       u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp)
    return -1;
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -1;
  }

  struct sockaddr_in sin;
  if (addrlen < sizeof(struct sockaddr_in) ||
      copyin((const void *)addr_ptr, &sin, sizeof(sin)) != XIU_SUCCESS) {
    fp_release(fp);
    return -1;
  }

  xiu_error_t err = soconnect(fp->fp_socket, (struct sockaddr *)&sin);
  fp_release(fp);
  return (err == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_listen(u64 fd_u, u64 backlog, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp)
    return -1;
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -1;
  }

  xiu_error_t err = solisten(fp->fp_socket, (int)backlog);
  fp_release(fp);
  return (err == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_accept(u64 fd_u, u64 addr_out, u64 addrlen_out, u64 a4, u64 a5,
                      u64 a6) {
  (void)addr_out;
  (void)addrlen_out;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp)
    return -1;
  fp_release(fp);
  return -1; // enotsup
}

static i64 sys_sendto(u64 fd_u, u64 buf_ptr, u64 len, u64 flags_u, u64 dest_ptr,
                      u64 addrlen) {
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp)
    return -1;
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -1;
  }

  char kbuf[1500];
  usize clen = (len > sizeof(kbuf)) ? sizeof(kbuf) : len;
  if (copyin((const void *)buf_ptr, kbuf, clen) != XIU_SUCCESS) {
    fp_release(fp);
    return -1;
  }

  struct sockaddr_in sin;
  struct sockaddr *saddr_ptr = nullptr;
  if (dest_ptr && addrlen >= sizeof(sin)) {
    if (copyin((const void *)dest_ptr, &sin, sizeof(sin)) == XIU_SUCCESS) {
      saddr_ptr = (struct sockaddr *)&sin;
    }
  }

  xiu_error_t err = sosend(fp->fp_socket, saddr_ptr, kbuf, clen, (int)flags_u);
  fp_release(fp);
  return (err == XIU_SUCCESS) ? (i64)clen : -1;
}

static i64 sys_recvfrom(u64 fd_u, u64 buf_ptr, u64 len, u64 flags_u,
                        u64 src_ptr, u64 addrlen_ptr) {
  (void)src_ptr;
  (void)addrlen_ptr;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp)
    return -1;
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -1;
  }

  char kbuf[1500];
  usize clen = (len > sizeof(kbuf)) ? sizeof(kbuf) : len;
  usize bytes_read = 0;
  xiu_error_t err =
      soreceive(fp->fp_socket, nullptr, kbuf, clen, &bytes_read, (int)flags_u);
  if (err == XIU_SUCCESS && bytes_read > 0) {
    copyout(kbuf, (void *)buf_ptr, bytes_read);
  }
  fp_release(fp);
  return (err == XIU_SUCCESS) ? (i64)bytes_read : -1;
}

static i64 sys_shutdown(u64 fd_u, u64 how, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp)
    return -1;
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -1;
  }

  xiu_error_t err = soshutdown(fp->fp_socket, (int)how);
  fp_release(fp);
  return (err == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_setsockopt(u64 fd_u, u64 level, u64 optname, u64 optval,
                          u64 optlen, u64 a6) {
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp)
    return -9; // EBADF
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -88; // ENOTSOCK
  }

  if (!optval || optlen == 0 || optlen > 256) {
    fp_release(fp);
    return -22; // EINVAL
  }

  u8 koptbuf[256];
  if (copyin((const void *)optval, koptbuf, (usize)optlen) != XIU_SUCCESS) {
    fp_release(fp);
    return -14; // EFAULT
  }

  extern xiu_error_t sosetopt(socket_t * so, int level, int optname,
                              const void *optval, usize optlen);
  xiu_error_t err =
      sosetopt(fp->fp_socket, (int)level, (int)optname, koptbuf, (usize)optlen);
  fp_release(fp);

  if (err == XIU_SUCCESS)
    return 0;
  if (err == XIU_ERR_NOTSUP)
    return -92; // ENOPROTOOPT
  return -22;   // EINVAL
}

static i64 sys_getsockopt(u64 fd_u, u64 level, u64 optname, u64 optval,
                          u64 optlen_ptr, u64 a6) {
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp)
    return -9; // EBADF
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -88; // ENOTSOCK
  }

  if (!optval || !optlen_ptr) {
    fp_release(fp);
    return -22; // EINVAL
  }

  u32 ulen = 0;
  if (copyin((const void *)optlen_ptr, &ulen, sizeof(u32)) != XIU_SUCCESS) {
    fp_release(fp);
    return -14; // EFAULT
  }

  if (ulen == 0 || ulen > 256) {
    fp_release(fp);
    return -22; // EINVAL
  }

  u8 koptbuf[256];
  usize optlen = (usize)ulen;
  extern xiu_error_t sogetopt(socket_t * so, int level, int optname,
                              void *optval, usize *optlen);
  xiu_error_t err =
      sogetopt(fp->fp_socket, (int)level, (int)optname, koptbuf, &optlen);
  fp_release(fp);

  if (err != XIU_SUCCESS) {
    if (err == XIU_ERR_NOTSUP)
      return -92; // ENOPROTOOPT
    return -22;   // EINVAL
  }

  if (copyout(koptbuf, (void *)optval, optlen) != XIU_SUCCESS)
    return -14;
  u32 final_len = (u32)optlen;
  if (copyout(&final_len, (void *)optlen_ptr, sizeof(u32)) != XIU_SUCCESS)
    return -14;

  return 0;
}

// ── POSIX Access, Dup, Vector I/O, Credentials, and Sysctl ──────────────────

static i64 sys_access(u64 path_ptr, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || !path_ptr)
    return -14; // EFAULT

  char path[256];
  if (copyin((const void *)path_ptr, path, sizeof(path)) != XIU_SUCCESS)
    return -14;
  path[sizeof(path) - 1] = '\0';

  char norm_path[256];
  resolve_relative_path(proc, path, norm_path, sizeof(norm_path));

  vnode_t *vp = nullptr;
  if (vfs_lookup(norm_path, &vp) != XIU_SUCCESS || !vp) {
    return -2; // ENOENT
  }

  if (mode == 0) {
    return 0; // F_OK: existence verified
  }

  u32 vmode = vp->v_attr.va_mode ? vp->v_attr.va_mode : 0755;
  if (proc->p_uid == 0) {
    if ((mode & 1) && !(vmode & 0111) && vp->v_type != VDIR) {
      return -13; // EACCES
    }
    return 0;
  }

  u32 granted = 0;
  if (proc->p_uid == vp->v_attr.va_uid) {
    if (vmode & 0400)
      granted |= 4;
    if (vmode & 0200)
      granted |= 2;
    if (vmode & 0100)
      granted |= 1;
  } else if (proc->p_gid == vp->v_attr.va_gid) {
    if (vmode & 0040)
      granted |= 4;
    if (vmode & 0020)
      granted |= 2;
    if (vmode & 0010)
      granted |= 1;
  } else {
    if (vmode & 0004)
      granted |= 4;
    if (vmode & 0002)
      granted |= 2;
    if (vmode & 0001)
      granted |= 1;
  }

  if ((mode & granted) != mode) {
    return -13; // EACCES
  }

  return 0;
}

static i64 sys_dup(u64 fd_u, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int fd = (int)fd_u;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || fd < 0 || fd >= XIU_PROC_MAX_FDS)
    return -9; // EBADF

  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp)
    return -9;

  int newfd = proc_fd_alloc_from(proc, fp, 0);
  fp_release(fp);
  return (newfd >= 0) ? (i64)newfd : -24; // EMFILE
}

struct xiu_iovec {
  u64 iov_base;
  u64 iov_len;
};

static i64 sys_readv(u64 fd_u, u64 iov_ptr, u64 iovcnt_u, u64 a4, u64 a5,
                     u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  if (!iov_ptr || iovcnt_u == 0 || iovcnt_u > 1024)
    return -22; // EINVAL

  struct xiu_iovec iov[16];
  usize cnt = (iovcnt_u > 16) ? 16 : (usize)iovcnt_u;
  if (copyin((const void *)iov_ptr, iov, cnt * sizeof(struct xiu_iovec)) !=
      XIU_SUCCESS)
    return -14;

  i64 total = 0;
  for (usize i = 0; i < cnt; i++) {
    if (iov[i].iov_len == 0)
      continue;
    i64 ret = sys_read(fd_u, iov[i].iov_base, iov[i].iov_len, 0, 0, 0);
    if (ret < 0) {
      if (total > 0)
        return total;
      return ret;
    }
    total += ret;
    if ((usize)ret < iov[i].iov_len)
      break;
  }
  return total;
}

static i64 sys_writev(u64 fd_u, u64 iov_ptr, u64 iovcnt_u, u64 a4, u64 a5,
                      u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  if (!iov_ptr || iovcnt_u == 0 || iovcnt_u > 1024)
    return -22; // EINVAL

  struct xiu_iovec iov[16];
  usize cnt = (iovcnt_u > 16) ? 16 : (usize)iovcnt_u;
  if (copyin((const void *)iov_ptr, iov, cnt * sizeof(struct xiu_iovec)) !=
      XIU_SUCCESS)
    return -14;

  i64 total = 0;
  for (usize i = 0; i < cnt; i++) {
    if (iov[i].iov_len == 0)
      continue;
    i64 ret = sys_write(fd_u, iov[i].iov_base, iov[i].iov_len, 0, 0, 0);
    if (ret < 0) {
      if (total > 0)
        return total;
      return ret;
    }
    total += ret;
    if ((usize)ret < iov[i].iov_len)
      break;
  }
  return total;
}

static i64 sys_pread(u64 fd_u, u64 buf, u64 len, u64 offset_u, u64 a5, u64 a6) {
  (void)a5;
  (void)a6;
  int fd = (int)fd_u;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || fd < 0 || fd >= XIU_PROC_MAX_FDS)
    return -9;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp)
    return -9;
  vnode_t *vp = fp->fp_vnode;
  if (!vp || !vp->v_op || !vp->v_op->vop_read) {
    fp_release(fp);
    return -1;
  }

  struct uio uio;
  uio.uio_buf = (void *)buf;
  uio.uio_resid = len;
  uio.uio_offset = offset_u;
  xiu_error_t err = vp->v_op->vop_read(vp, &uio, 0, nullptr);
  fp_release(fp);
  return (err == XIU_SUCCESS) ? (i64)(len - uio.uio_resid) : -1;
}

static i64 sys_pwrite(u64 fd_u, u64 buf, u64 len, u64 offset_u, u64 a5,
                      u64 a6) {
  (void)a5;
  (void)a6;
  int fd = (int)fd_u;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || fd < 0 || fd >= XIU_PROC_MAX_FDS)
    return -9;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp)
    return -9;
  vnode_t *vp = fp->fp_vnode;
  if (!vp || !vp->v_op || !vp->v_op->vop_write) {
    fp_release(fp);
    return -1;
  }

  struct uio uio;
  uio.uio_buf = (void *)buf;
  uio.uio_resid = len;
  uio.uio_offset = offset_u;
  xiu_error_t err = vp->v_op->vop_write(vp, &uio, 0, nullptr);
  fp_release(fp);
  return (err == XIU_SUCCESS) ? (i64)(len - uio.uio_resid) : -1;
}

static i64 sys_fstat(u64 fd_u, u64 statbuf, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int fd = (int)fd_u;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || fd < 0 || fd >= XIU_PROC_MAX_FDS || !statbuf)
    return -9;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp)
    return -9;

  xiu_user_stat_t st;
  __builtin_memset(&st, 0, sizeof(st));
  st.st_dev = 1;
  st.st_ino = (u64)((uptr)fp & 0xFFFFFFFFu);
  st.st_nlink = 1;
  st.st_uid = 0;
  st.st_gid = 0;
  st.st_blksize = 4096;

  if (fp->fp_type == DTYPE_SOCKET) {
    st.st_mode = 0140666;
  } else if (fp->fp_type == DTYPE_PIPE) {
    st.st_mode = 0010666;
  } else if (fp->fp_vnode) {
    vnode_t *vp = fp->fp_vnode;
    st.st_mode = vnode_mode(vp);
    st.st_nlink = (vp->v_type == VDIR) ? 2 : 1;
    st.st_size = (i64)vp->v_attr.va_size;
    st.st_blocks = (st.st_size + 511) / 512;
  }

  fp_release(fp);
  return (copyout(&st, (void *)statbuf, sizeof(st)) == XIU_SUCCESS) ? 0 : -14;
}

static i64 sys_chmod(u64 path_ptr, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || !path_ptr)
    return -14;
  char path[256], norm[256];
  if (copyin((const void *)path_ptr, path, sizeof(path)) != XIU_SUCCESS)
    return -14;
  path[sizeof(path) - 1] = '\0';
  resolve_relative_path(proc, path, norm, sizeof(norm));
  vnode_t *vp = nullptr;
  if (vfs_lookup(norm, &vp) != XIU_SUCCESS || !vp)
    return -2;
  vp->v_attr.va_mode = (u16)(mode & 0777);
  return 0;
}

static i64 sys_fchmod(u64 fd_u, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int fd = (int)fd_u;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || fd < 0 || fd >= XIU_PROC_MAX_FDS)
    return -9;
  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp)
    return -9;
  if (fp->fp_vnode)
    fp->fp_vnode->v_attr.va_mode = (u16)(mode & 0777);
  fp_release(fp);
  return 0;
}

static i64 sys_chown(u64 path_ptr, u64 uid, u64 gid, u64 a4, u64 a5, u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || !path_ptr)
    return -14;
  char path[256], norm[256];
  if (copyin((const void *)path_ptr, path, sizeof(path)) != XIU_SUCCESS)
    return -14;
  path[sizeof(path) - 1] = '\0';
  resolve_relative_path(proc, path, norm, sizeof(norm));
  vnode_t *vp = nullptr;
  if (vfs_lookup(norm, &vp) != XIU_SUCCESS || !vp)
    return -2;
  vp->v_attr.va_uid = (xiu_uid_t)uid;
  vp->v_attr.va_gid = (xiu_gid_t)gid;
  return 0;
}

static i64 sys_fchown(u64 fd_u, u64 uid, u64 gid, u64 a4, u64 a5, u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  int fd = (int)fd_u;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || fd < 0 || fd >= XIU_PROC_MAX_FDS)
    return -9;
  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp)
    return -9;
  if (fp->fp_vnode) {
    fp->fp_vnode->v_attr.va_uid = (xiu_uid_t)uid;
    fp->fp_vnode->v_attr.va_gid = (xiu_gid_t)gid;
  }
  fp_release(fp);
  return 0;
}

static i64 sys_truncate(u64 path_ptr, u64 length, u64 a3, u64 a4, u64 a5,
                        u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || !path_ptr)
    return -14;

  char path[256];
  if (copyin((const void *)path_ptr, path, sizeof(path)) != XIU_SUCCESS)
    return -14;
  path[sizeof(path) - 1] = '\0';

  char norm_path[256];
  resolve_relative_path(proc, path, norm_path, sizeof(norm_path));

  vnode_t *vp = nullptr;
  if (vfs_lookup(norm_path, &vp) != XIU_SUCCESS || !vp)
    return -2;

  vp->v_attr.va_size = length;
  return 0;
}

static i64 sys_ftruncate(u64 fd_u, u64 length, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int fd = (int)fd_u;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || fd < 0 || fd >= XIU_PROC_MAX_FDS)
    return -9;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp)
    return -9;
  if (fp->fp_vnode) {
    fp->fp_vnode->v_attr.va_size = length;
  }
  fp_release(fp);
  return 0;
}

static i64 sys_rename(u64 old_ptr, u64 new_ptr, u64 a3, u64 a4, u64 a5,
                      u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || !old_ptr || !new_ptr)
    return -14;

  char oldpath[256], newpath[256];
  if (copyin((const void *)old_ptr, oldpath, sizeof(oldpath)) != XIU_SUCCESS ||
      copyin((const void *)new_ptr, newpath, sizeof(newpath)) != XIU_SUCCESS)
    return -14;
  oldpath[sizeof(oldpath) - 1] = '\0';
  newpath[sizeof(newpath) - 1] = '\0';

  char norm_old[256], norm_new[256];
  resolve_relative_path(proc, oldpath, norm_old, sizeof(norm_old));
  resolve_relative_path(proc, newpath, norm_new, sizeof(norm_new));

  extern xiu_error_t vfs_rename_node(const char *oldpath, const char *newpath);
  xiu_error_t err = vfs_rename_node(norm_old, norm_new);
  if (err != XIU_SUCCESS)
    return -2; // ENOENT
  return 0;
}

static i64 sys_link(u64 old_ptr, u64 new_ptr, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!old_ptr || !new_ptr)
    return -14; // EFAULT
  char oldpath[256], newpath[256];
  if (copyin((const void *)old_ptr, oldpath, sizeof(oldpath)) != XIU_SUCCESS ||
      copyin((const void *)new_ptr, newpath, sizeof(newpath)) != XIU_SUCCESS)
    return -14;
  oldpath[sizeof(oldpath) - 1] = '\0';
  newpath[sizeof(newpath) - 1] = '\0';

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  char norm_old[256];
  resolve_relative_path(proc, oldpath, norm_old, sizeof(norm_old));
  vnode_t *vp = nullptr;
  if (vfs_lookup(norm_old, &vp) != XIU_SUCCESS || !vp)
    return -2; // ENOENT

  // FAT32 does not support hard links -> POSIX requires -EPERM or -ENOTSUP
  return -1; // -EPERM
}

static i64 sys_mknod(u64 path_ptr, u64 mode, u64 dev, u64 a4, u64 a5, u64 a6) {
  (void)dev;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!path_ptr)
    return -14; // EFAULT
  char path[256];
  if (copyin((const void *)path_ptr, path, sizeof(path)) != XIU_SUCCESS)
    return -14;
  path[sizeof(path) - 1] = '\0';

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  if (proc->p_uid != 0)
    return -1; // -EPERM

  if ((mode & 0170000) == 0040000)
    return -22; // EINVAL

  // FAT32 does not support creating special device/fifo nodes on disk
  return -1; // -EPERM
}

static i64 sys_fsync(u64 fd_u, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  int fd = (int)fd_u;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || fd < 0 || fd >= XIU_PROC_MAX_FDS)
    return -9; // EBADF

  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp)
    return -9;

  if (fp->fp_type == DTYPE_VNODE && fp->fp_vnode) {
    vnode_t *vp = fp->fp_vnode;
    if (vp->v_mount && vp->v_mount->mnt_op && vp->v_mount->mnt_op->mop_sync) {
      vp->v_mount->mnt_op->mop_sync(vp->v_mount, 1, nullptr);
    }
  }

  fp_release(fp);
  return 0;
}

static i64 sys_umask(u64 newmask, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (!t || !t->ta_proc)
    return 022;
  u32 old = t->ta_proc->p_umask;
  t->ta_proc->p_umask = (u32)(newmask & 0777);
  return (i64)old;
}

static i64 sys_getuid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  return (t && t->ta_proc) ? (i64)t->ta_proc->p_uid : 0;
}
static i64 sys_geteuid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  return (t && t->ta_proc) ? (i64)t->ta_proc->p_euid : 0;
}
static i64 sys_setuid(u64 uid, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (t && t->ta_proc) {
    t->ta_proc->p_uid = (xiu_uid_t)uid;
    t->ta_proc->p_euid = (xiu_uid_t)uid;
    return 0;
  }
  return -1;
}
static i64 sys_seteuid(u64 euid, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (t && t->ta_proc) {
    t->ta_proc->p_euid = (xiu_uid_t)euid;
    return 0;
  }
  return -1;
}
static i64 sys_getgid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  return (t && t->ta_proc) ? (i64)t->ta_proc->p_gid : 0;
}
static i64 sys_getegid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  return (t && t->ta_proc) ? (i64)t->ta_proc->p_egid : 0;
}
static i64 sys_setgid(u64 gid, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (t && t->ta_proc) {
    t->ta_proc->p_gid = (xiu_gid_t)gid;
    t->ta_proc->p_egid = (xiu_gid_t)gid;
    return 0;
  }
  return -1;
}
static i64 sys_setegid(u64 egid, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (t && t->ta_proc) {
    t->ta_proc->p_egid = (xiu_gid_t)egid;
    return 0;
  }
  return -1;
}
static i64 sys_getlogin(u64 name_ptr, u64 namelen, u64 a3, u64 a4, u64 a5,
                        u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (!t || !t->ta_proc || !name_ptr || namelen == 0)
    return -1;
  xiu_proc_t *p = t->ta_proc;
  usize len = 0;
  while (len < 31 && len < namelen - 1 && p->p_login[len]) {
    len++;
  }
  char buf[32];
  __builtin_memcpy(buf, p->p_login, len);
  buf[len] = '\0';
  if (copyout(buf, (void *)name_ptr, len + 1) != XIU_SUCCESS) {
    return -1;
  }
  return 0;
}

static i64 sys_setlogin(u64 name_ptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (!t || !t->ta_proc || !name_ptr)
    return -1;
  xiu_proc_t *p = t->ta_proc;
  if (p->p_uid != 0 && p->p_euid != 0)
    return -1;
  char buf[32];
  usize copied = 0;
  if (copyinstr((const void *)name_ptr, buf, sizeof(buf), &copied) !=
      XIU_SUCCESS) {
    return -1;
  }
  buf[31] = '\0';
  __builtin_memcpy(p->p_login, buf, sizeof(p->p_login));
  return 0;
}

static i64 sys_getgroups(u64 size, u64 list_ptr, u64 a3, u64 a4, u64 a5,
                         u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (!t || !t->ta_proc)
    return -1;
  xiu_proc_t *p = t->ta_proc;
  if (size == 0)
    return (i64)p->p_ngroups;
  if (!list_ptr)
    return -14;
  u32 to_copy = (p->p_ngroups < (u32)size) ? p->p_ngroups : (u32)size;
  if (copyout(p->p_groups, (void *)list_ptr, to_copy * sizeof(xiu_gid_t)) !=
      XIU_SUCCESS)
    return -14;
  return (i64)to_copy;
}
static i64 sys_setgroups(u64 size, u64 list_ptr, u64 a3, u64 a4, u64 a5,
                         u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (!t || !t->ta_proc)
    return -1;
  if (size > 16)
    return -22;
  if (size > 0 && !list_ptr)
    return -14;
  xiu_proc_t *p = t->ta_proc;
  p->p_ngroups = (u32)size;
  if (size > 0) {
    if (copyin((const void *)list_ptr, p->p_groups, size * sizeof(xiu_gid_t)) !=
        XIU_SUCCESS)
      return -14;
  }
  return 0;
}
static i64 sys_getppid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  return (t && t->ta_proc) ? (i64)t->ta_proc->p_ppid : 0;
}
static i64 sys_getpgrp(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  return (t && t->ta_proc) ? (i64)t->ta_proc->p_pgrp : 0;
}
static i64 sys_setpgid(u64 pid_u, u64 pgrp_u, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (!t || !t->ta_proc)
    return -1;
  xiu_proc_t *target = (pid_u == 0 || pid_u == t->ta_proc->p_pid)
                           ? t->ta_proc
                           : proc_find_by_pid((xiu_pid_t)pid_u);
  if (!target)
    return -3;
  target->p_pgrp = (pgrp_u == 0) ? target->p_pid : (xiu_pid_t)pgrp_u;
  return 0;
}
static i64 sys_setsid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (!t || !t->ta_proc)
    return -1;
  t->ta_proc->p_sid = t->ta_proc->p_pid;
  t->ta_proc->p_pgrp = t->ta_proc->p_pid;
  return (i64)t->ta_proc->p_pid;
}

struct xiu_timeval {
  i64 tv_sec;
  i64 tv_usec;
};

static i64 s_time_base_sec = 1700000000ULL;
static i64 s_time_base_usec = 0;

static i64 sys_gettimeofday(u64 tv_ptr, u64 tz_ptr, u64 a3, u64 a4, u64 a5,
                            u64 a6) {
  (void)tz_ptr;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!tv_ptr)
    return -14;

  extern u64 timer_get_uptime_seconds(void);
  extern u64 timer_get_uptime_ns(void);

  u64 uptime_sec = timer_get_uptime_seconds();
  u64 uptime_ns = timer_get_uptime_ns();
  u64 uptime_usec = (uptime_ns % 1000000000ULL) / 1000ULL;

  struct xiu_timeval tv;
  i64 total_usec = s_time_base_usec + (i64)uptime_usec;
  tv.tv_sec = s_time_base_sec + (i64)uptime_sec + (total_usec / 1000000);
  tv.tv_usec = total_usec % 1000000;

  return (copyout(&tv, (void *)tv_ptr, sizeof(tv)) == XIU_SUCCESS) ? 0 : -14;
}

static i64 sys_settimeofday(u64 tv_ptr, u64 tz_ptr, u64 a3, u64 a4, u64 a5,
                            u64 a6) {
  (void)tz_ptr;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || proc->p_uid != 0)
    return -1; // -EPERM

  if (tv_ptr) {
    struct xiu_timeval tv;
    if (copyin((const void *)tv_ptr, &tv, sizeof(tv)) != XIU_SUCCESS)
      return -14;
    extern u64 timer_get_uptime_seconds(void);
    extern u64 timer_get_uptime_ns(void);
    u64 uptime_sec = timer_get_uptime_seconds();
    u64 uptime_usec = (timer_get_uptime_ns() % 1000000000ULL) / 1000ULL;
    s_time_base_sec = tv.tv_sec - (i64)uptime_sec;
    s_time_base_usec = tv.tv_usec - (i64)uptime_usec;
    while (s_time_base_usec < 0) {
      s_time_base_usec += 1000000;
      s_time_base_sec -= 1;
    }
  }
  return 0;
}

struct xiu_pollfd {
  i32 fd;
  i16 events;
  i16 revents;
};

#define XIU_POLLIN 0x0001
#define XIU_POLLPRI 0x0002
#define XIU_POLLOUT 0x0004
#define XIU_POLLERR 0x0008
#define XIU_POLLHUP 0x0010
#define XIU_POLLNVAL 0x0020

extern i16 fileproc_poll(xiu_fileproc_t *fp, i16 events);
extern void xiukit_hid_poll(void);

static i64 sys_poll(u64 fds_ptr, u64 nfds_u, u64 timeout_ms_u, u64 a4, u64 a5,
                    u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  if (!fds_ptr && nfds_u > 0)
    return -14; // EFAULT
  u32 nfds = (u32)nfds_u;
  if (nfds > 256)
    return -22; // EINVAL

  struct xiu_pollfd fds[32];
  u32 count = (nfds > 32) ? 32 : nfds;
  if (count > 0) {
    if (copyin((const void *)fds_ptr, fds, count * sizeof(struct xiu_pollfd)) !=
        XIU_SUCCESS)
      return -14;
  }

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  extern u64 timer_get_uptime_ms(void);
  i32 timeout_ms = (i32)timeout_ms_u;
  u64 start_ms = timer_get_uptime_ms();

  for (;;) {
    if (proc->p_sigpending & ~proc->p_sigmask) {
      return -4; // -EINTR
    }

    i32 ready_count = 0;
    for (u32 i = 0; i < count; i++) {
      fds[i].revents = 0;
      if (fds[i].fd < 0)
        continue;
      if (fds[i].fd >= XIU_PROC_MAX_FDS) {
        fds[i].revents = XIU_POLLNVAL;
        ready_count++;
        continue;
      }

      xiu_fileproc_t *fp = proc_fd_lookup(proc, fds[i].fd);
      if (!fp) {
        fds[i].revents = XIU_POLLNVAL;
        ready_count++;
        continue;
      }

      i16 rev = fileproc_poll(fp, fds[i].events);
      fp_release(fp);

      if (rev) {
        fds[i].revents = rev;
        ready_count++;
      }
    }

    if (ready_count > 0 || timeout_ms == 0) {
      if (count > 0) {
        copyout(fds, (void *)fds_ptr, count * sizeof(struct xiu_pollfd));
      }
      return ready_count;
    }

    if (timeout_ms > 0 &&
        (timer_get_uptime_ms() - start_ms) >= (u64)timeout_ms) {
      if (count > 0) {
        copyout(fds, (void *)fds_ptr, count * sizeof(struct xiu_pollfd));
      }
      return 0;
    }

    xiukit_hid_poll();
    scheduler_yield();
  }
}

typedef struct {
  u32 fds_bits[8];
} xiu_fd_set;

static i64 sys_select(u64 nfds_u, u64 readfds_ptr, u64 writefds_ptr,
                      u64 exceptfds_ptr, u64 timeout_ptr, u64 a6) {
  (void)a6;
  int nfds = (int)nfds_u;
  if (nfds < 0 || nfds > XIU_PROC_MAX_FDS)
    return -22; // EINVAL

  xiu_fd_set rfds, wfds, efds;
  __builtin_memset(&rfds, 0, sizeof(rfds));
  __builtin_memset(&wfds, 0, sizeof(wfds));
  __builtin_memset(&efds, 0, sizeof(efds));

  if (readfds_ptr &&
      copyin((const void *)readfds_ptr, &rfds, sizeof(rfds)) != XIU_SUCCESS)
    return -14;
  if (writefds_ptr &&
      copyin((const void *)writefds_ptr, &wfds, sizeof(wfds)) != XIU_SUCCESS)
    return -14;
  if (exceptfds_ptr &&
      copyin((const void *)exceptfds_ptr, &efds, sizeof(efds)) != XIU_SUCCESS)
    return -14;

  struct xiu_timeval tv;
  i32 timeout_ms = -1;
  if (timeout_ptr) {
    if (copyin((const void *)timeout_ptr, &tv, sizeof(tv)) != XIU_SUCCESS)
      return -14;
    timeout_ms = (i32)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
  }

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  extern u64 timer_get_uptime_ms(void);
  u64 start_ms = timer_get_uptime_ms();

  for (;;) {
    if (proc->p_sigpending & ~proc->p_sigmask) {
      return -4; // -EINTR
    }

    xiu_fd_set out_rfds, out_wfds, out_efds;
    __builtin_memset(&out_rfds, 0, sizeof(out_rfds));
    __builtin_memset(&out_wfds, 0, sizeof(out_wfds));
    __builtin_memset(&out_efds, 0, sizeof(out_efds));

    int ready_count = 0;
    for (int fd = 0; fd < nfds; fd++) {
      int idx = fd / 32;
      int bit = fd % 32;

      i16 events = 0;
      if (readfds_ptr && (rfds.fds_bits[idx] & (1U << bit)))
        events |= XIU_POLLIN;
      if (writefds_ptr && (wfds.fds_bits[idx] & (1U << bit)))
        events |= XIU_POLLOUT;
      if (exceptfds_ptr && (efds.fds_bits[idx] & (1U << bit)))
        events |= XIU_POLLPRI;

      if (!events)
        continue;

      xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
      if (!fp) {
        return -9; // EBADF
      }

      i16 rev = fileproc_poll(fp, events);
      fp_release(fp);

      if ((events & XIU_POLLIN) &&
          (rev & (XIU_POLLIN | XIU_POLLHUP | XIU_POLLERR))) {
        out_rfds.fds_bits[idx] |= (1U << bit);
        ready_count++;
      }
      if ((events & XIU_POLLOUT) && (rev & (XIU_POLLOUT | XIU_POLLERR))) {
        out_wfds.fds_bits[idx] |= (1U << bit);
        ready_count++;
      }
      if ((events & XIU_POLLPRI) && (rev & (XIU_POLLPRI | XIU_POLLERR))) {
        out_efds.fds_bits[idx] |= (1U << bit);
        ready_count++;
      }
    }

    if (ready_count > 0 || timeout_ms == 0) {
      if (readfds_ptr)
        copyout(&out_rfds, (void *)readfds_ptr, sizeof(out_rfds));
      if (writefds_ptr)
        copyout(&out_wfds, (void *)writefds_ptr, sizeof(out_wfds));
      if (exceptfds_ptr)
        copyout(&out_efds, (void *)exceptfds_ptr, sizeof(out_efds));
      return ready_count;
    }

    if (timeout_ms > 0 &&
        (timer_get_uptime_ms() - start_ms) >= (u64)timeout_ms) {
      if (readfds_ptr)
        copyout(&out_rfds, (void *)readfds_ptr, sizeof(out_rfds));
      if (writefds_ptr)
        copyout(&out_wfds, (void *)writefds_ptr, sizeof(out_wfds));
      if (exceptfds_ptr)
        copyout(&out_efds, (void *)exceptfds_ptr, sizeof(out_efds));
      return 0;
    }

    xiukit_hid_poll();
    scheduler_yield();
  }
}

static i64 sys_socketpair(u64 dom, u64 type, u64 proto, u64 sv_ptr, u64 a5,
                          u64 a6) {
  (void)dom;
  (void)type;
  (void)proto;
  (void)a5;
  (void)a6;
  if (!sv_ptr)
    return -14;
  return sys_pipe(sv_ptr, 0, 0, 0, 0, 0);
}

#define CTL_KERN 1
#define CTL_HW 6

#define KERN_OSTYPE 1
#define KERN_OSRELEASE 2
#define KERN_OSREV 3
#define KERN_VERSION 4
#define KERN_MAXVNODES 5
#define KERN_MAXPROC 6
#define KERN_ARGMAX 8
#define KERN_HOSTNAME 10

#define HW_MACHINE 1
#define HW_MODEL 2
#define HW_NCPU 3
#define HW_BYTEORDER 4
#define HW_PHYSMEM 5
#define HW_USERMEM 6
#define HW_PAGESIZE 7
#define HW_MEMSIZE 24
#define HW_AVAILCPU 25

static char s_kernel_hostname[64] = "Mac";

static i64 sys_mprotect(u64 addr, u64 len, u64 prot, u64 a4, u64 a5, u64 a6) {
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *task = current_task();
  if (!task || !task->ta_vm_map || len == 0)
    return -22;
  extern int pmap_protect_user_range(u64 pml4_phys, u64 virt_start, usize len,
                                     u32 prot);
  int rc = pmap_protect_user_range((u64)task->ta_vm_map, addr, (usize)len,
                                   (u32)prot);
  return (rc == 0) ? 0 : -14;
}

static i64 sys_getpgid(u64 pid_u, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (!t || !t->ta_proc)
    return -1;
  xiu_proc_t *target = (pid_u == 0 || pid_u == t->ta_proc->p_pid)
                           ? t->ta_proc
                           : proc_find_by_pid((xiu_pid_t)pid_u);
  if (!target)
    return -3;
  return (i64)target->p_pgrp;
}

static i64 sys_getsid(u64 pid_u, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  xiu_task_t *t = current_task();
  if (!t || !t->ta_proc)
    return -1;
  xiu_proc_t *target = (pid_u == 0 || pid_u == t->ta_proc->p_pid)
                           ? t->ta_proc
                           : proc_find_by_pid((xiu_pid_t)pid_u);
  if (!target)
    return -3;
  return (i64)target->p_sid;
}

static i64 sys_sysctl(u64 name_ptr, u64 namelen, u64 oldp, u64 oldlenp,
                      u64 newp, u64 newlen) {
  if (!name_ptr || namelen < 2)
    return -22;

  int mib[8];
  usize mib_len = (namelen > 8) ? 8 : (usize)namelen;
  if (copyin((const void *)name_ptr, mib, mib_len * sizeof(int)) != XIU_SUCCESS)
    return -14;

  usize oldlen = 0;
  if (oldlenp) {
    if (copyin((const void *)oldlenp, &oldlen, sizeof(usize)) != XIU_SUCCESS)
      return -14;
  }

  int ctl = mib[0];
  int sub = mib[1];

  // handle write requests (e.g. sethostname)
  if (newp && newlen > 0) {
    xiu_task_t *t = current_task();
    if (!t || !t->ta_proc || t->ta_proc->p_uid != 0)
      return -1; // -EPERM
    if (ctl == CTL_KERN && sub == KERN_HOSTNAME) {
      usize copy_len = (newlen < sizeof(s_kernel_hostname) - 1)
                           ? newlen
                           : (sizeof(s_kernel_hostname) - 1);
      if (copyin((const void *)newp, s_kernel_hostname, copy_len) !=
          XIU_SUCCESS)
        return -14;
      s_kernel_hostname[copy_len] = '\0';
      return 0;
    }
    return -22;
  }

  const void *res_ptr = nullptr;
  usize res_sz = 0;
  int int_val = 0;
  u64 u64_val = 0;

  if (ctl == CTL_KERN) {
    if (sub == KERN_OSTYPE) {
      res_ptr = "Darwin";
      res_sz = 7;
    } else if (sub == KERN_OSRELEASE) {
      res_ptr = "24.0.0";
      res_sz = 7;
    } else if (sub == KERN_VERSION) {
      res_ptr = "Darwin Kernel Version 24.0.0: XIU Hybrid x86_64";
      res_sz = __builtin_strlen((const char *)res_ptr) + 1;
    } else if (sub == KERN_HOSTNAME) {
      res_ptr = s_kernel_hostname;
      res_sz = __builtin_strlen(s_kernel_hostname) + 1;
    } else if (sub == KERN_MAXVNODES) {
      int_val = 4096;
      res_ptr = &int_val;
      res_sz = sizeof(int);
    } else if (sub == KERN_MAXPROC) {
      int_val = 64;
      res_ptr = &int_val;
      res_sz = sizeof(int);
    } else if (sub == KERN_ARGMAX) {
      int_val = 65536;
      res_ptr = &int_val;
      res_sz = sizeof(int);
    }
  } else if (ctl == CTL_HW) {
    if (sub == HW_MACHINE) {
      res_ptr = "x86_64";
      res_sz = 7;
    } else if (sub == HW_MODEL) {
      res_ptr = "XIU-x86_64";
      res_sz = 11;
    } else if (sub == HW_NCPU || sub == HW_AVAILCPU) {
      extern u32 smp_get_active_cpus(void);
      int_val = (int)smp_get_active_cpus();
      if (int_val <= 0)
        int_val = 4;
      res_ptr = &int_val;
      res_sz = sizeof(int);
    } else if (sub == HW_PAGESIZE) {
      int_val = 4096;
      res_ptr = &int_val;
      res_sz = sizeof(int);
    } else if (sub == HW_BYTEORDER) {
      int_val = 1234;
      res_ptr = &int_val;
      res_sz = sizeof(int);
    } else if (sub == HW_MEMSIZE || sub == HW_PHYSMEM) {
      extern usize pmm_total_pages(void);
      u64_val = (u64)pmm_total_pages() * 4096ULL;
      res_ptr = &u64_val;
      res_sz = sizeof(u64);
    }
  }

  if (!res_ptr)
    return -2;

  if (oldp && oldlen > 0) {
    usize to_copy = (oldlen < res_sz) ? oldlen : res_sz;
    if (copyout(res_ptr, (void *)oldp, to_copy) != XIU_SUCCESS)
      return -14;
  }
  if (oldlenp) {
    if (copyout(&res_sz, (void *)oldlenp, sizeof(usize)) != XIU_SUCCESS)
      return -14;
  }
  return 0;
}

static i64 sys_sigpending(u64 set_ptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (!set_ptr)
    return -14;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  u32 pending = proc->p_sigpending;
  return (copyout(&pending, (void *)set_ptr, sizeof(u32)) == XIU_SUCCESS) ? 0
                                                                          : -14;
}

typedef struct xiu_stack {
  u64 ss_sp;
  u64 ss_size;
  i32 ss_flags;
} xiu_stack_t;

static i64 sys_sigaltstack(u64 ss_ptr, u64 oss_ptr, u64 a3, u64 a4, u64 a5,
                           u64 a6) {
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if (oss_ptr) {
    xiu_stack_t oss;
    __builtin_memset(&oss, 0, sizeof(oss));
    oss.ss_flags = 2; // SS_DISABLE
    if (copyout(&oss, (void *)oss_ptr, sizeof(oss)) != XIU_SUCCESS)
      return -14;
  }
  if (ss_ptr) {
    xiu_stack_t ss;
    if (copyin((const void *)ss_ptr, &ss, sizeof(ss)) != XIU_SUCCESS)
      return -14;
    if (!(ss.ss_flags & 2) && ss.ss_size < 2048)
      return -22; // EINVAL (MINSIGSTKSZ)
  }
  return 0;
}

// ── Standard Apple XNU Syscall Dispatch Table ────────────────────────────────

const syscall_fn_t g_syscall_table[512] = {
    [SYS_exit] = sys_exit,
    [SYS_fork] = sys_fork,
    [SYS_read] = sys_read,
    [SYS_write] = sys_write,
    [SYS_open] = sys_open,
    [SYS_close] = sys_close,
    [SYS_wait4] = sys_wait4,
    [SYS_link] = sys_link,
    [SYS_unlink] = sys_unlink,
    [SYS_chdir] = sys_chdir,
    [SYS_fchdir] = sys_chdir,
    [SYS_mknod] = sys_mknod,
    [SYS_chmod] = sys_chmod,
    [SYS_chown] = sys_chown,
    [SYS_getpid] = sys_getpid,
    [SYS_setuid] = sys_setuid,
    [SYS_getuid] = sys_getuid,
    [SYS_geteuid] = sys_geteuid,
    [SYS_recvfrom] = sys_recvfrom,
    [SYS_accept] = sys_accept,
    [SYS_access] = sys_access,
    [SYS_kill] = sys_kill,
    [SYS_getppid] = sys_getppid,
    [SYS_dup] = sys_dup,
    [SYS_pipe] = sys_pipe,
    [SYS_getegid] = sys_getegid,
    [SYS_sigaction] = sys_sigaction,
    [SYS_getgid] = sys_getgid,
    [SYS_sigprocmask] = sys_sigprocmask,
    [SYS_getlogin] = sys_getlogin,
    [SYS_setlogin] = sys_setlogin,
    [SYS_sigpending] = sys_sigpending,
    [SYS_sigaltstack] = sys_sigaltstack,
    [SYS_ioctl] = sys_ioctl,
    [SYS_execve] = sys_execve,
    [SYS_umask] = sys_umask,
    [SYS_munmap] = sys_munmap,
    [SYS_mprotect] = sys_mprotect,
    [SYS_getgroups] = sys_getgroups,
    [SYS_setgroups] = sys_setgroups,
    [SYS_getpgrp] = sys_getpgrp,
    [SYS_setpgid] = sys_setpgid,
    [SYS_dup2] = sys_dup2,
    [SYS_fcntl] = sys_fcntl,
    [SYS_select] = sys_select,
    [SYS_fsync] = sys_fsync,
    [SYS_socket] = sys_socket,
    [SYS_connect] = sys_connect,
    [SYS_bind] = sys_bind,
    [SYS_setsockopt] = sys_setsockopt,
    [SYS_listen] = sys_listen,
    [SYS_gettimeofday] = sys_gettimeofday,
    [SYS_getsockopt] = sys_getsockopt,
    [SYS_readv] = sys_readv,
    [SYS_writev] = sys_writev,
    [SYS_settimeofday] = sys_settimeofday,
    [SYS_fchown] = sys_fchown,
    [SYS_fchmod] = sys_fchmod,
    [SYS_rename] = sys_rename,
    [SYS_sendto] = sys_sendto,
    [SYS_shutdown] = sys_shutdown,
    [SYS_socketpair] = sys_socketpair,
    [SYS_mkdir] = sys_mkdir,
    [SYS_rmdir] = sys_rmdir,
    [SYS_setsid] = sys_setsid,
    [SYS_getpgid] = sys_getpgid,
    [SYS_getsid] = sys_getsid,
    [SYS_pread] = sys_pread,
    [SYS_pwrite] = sys_pwrite,
    [SYS_setgid] = sys_setgid,
    [SYS_setegid] = sys_setegid,
    [SYS_seteuid] = sys_seteuid,
    [SYS_stat] = sys_stat,
    [SYS_fstat] = sys_fstat,
    [SYS_lstat] = sys_stat,
    [SYS_getdirentries] = sys_getdents,
    [SYS_mmap] = sys_mmap,
    [SYS_lseek] = sys_lseek,
    [SYS_truncate] = sys_truncate,
    [SYS_ftruncate] = sys_ftruncate,
    [SYS_sysctl] = sys_sysctl,
    [SYS_poll] = sys_poll,
    [SYS_nanosleep] = sys_nanosleep,
    [SYS_posix_spawn] = sys_spawn,
    [SYS_sched_yield] = sys_yield,
    [SYS_spawn] = sys_spawn,
    [SYS_sysinfo] = sys_sysinfo,
    [SYS_proclist] = sys_proclist,
    [SYSCALL_LOG] = sys_log,
    [SYS_stat64] = sys_stat,
    [SYS_fstat64] = sys_fstat,
    [SYS_lstat64] = sys_stat,
    [SYS_getcwd] = sys_getcwd,

    // Mach IPC & system traps
    [SYS_mach_msg] = sys_mach_msg,
    [SYS_mach_port_alloc] = sys_mach_port_allocate,
    [SYS_mach_register] = sys_mach_register_service,
    [SYS_mach_lookup] = sys_mach_lookup_service,
    [SYS_mach_port_dealloc] = sys_mach_port_deallocate,
    [SYS_mach_port_type] = sys_mach_port_type,
    [SYS_task_self] = sys_task_self,
};

const u32 g_syscall_count = 512;

// dispatcher

i64 syscall_dispatch(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5,
                     u64 arg6, u64 frame) {
  g_syscall_frame = frame;

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;

  if (num >= g_syscall_count || g_syscall_table[num] == nullptr) {
    kprintf("[SYSCALL] Invalid syscall %llu from '%s' (PID %d)\n",
            (unsigned long long)num, proc ? proc->p_comm : "?",
            proc ? proc->p_pid : 0);
    g_syscall_frame = 0;
    return -78; // ENOSYS
  }

  i64 ret = g_syscall_table[num](arg1, arg2, arg3, arg4, arg5, arg6);

  if (frame) {
    extern void proc_deliver_signals(void *frame_ptr);
    proc_deliver_signals((void *)frame);
  }

  g_syscall_frame = 0;
  return ret;
}
