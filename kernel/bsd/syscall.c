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

#include <kernel/mach_o.h>
#include <kernel/fb.h>
#include <kernel/vfs_node.h>
#include <kernel/xiu_types.h>
#include <net/socket.h>
#include <net/protocols.h>

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

static u64 g_syscall_frame;

typedef struct syscall_user_frame {
  u64 r15, r14, r13, r12, rbx, rbp;
  u64 rip;
  u64 rflags;
  u64 rsp; // user RSP - pushed FIRST in syscall_entry.S
} syscall_user_frame_t;

typedef struct xiu_user_dirent {
  u64 d_ino;
  u64 d_off;
  u16 d_reclen;
  u8 d_type;
  char d_name[256];
} xiu_user_dirent_t;

typedef struct xiu_user_stat {
  u32 st_dev, st_ino, st_mode, st_nlink, st_uid, st_gid, st_rdev;
  u64 st_size, st_atime, st_mtime, st_ctime;
  u32 st_blksize, st_blocks;
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

static u32 vnode_mode(vnode_t *vp) {
  u32 perm = 0755;
  if (!vp)
    return perm;
  if (vp->v_type == VDIR)
    return 0040000 | perm;
  if (vp->v_type == VCHR)
    return 0020000 | 0666;
  return 0100000 | perm;
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
 * Marks the task as exited, frees descriptors, unlinks from scheduler, and yields.
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

  // lookup the target directory with relative path resolution
  vnode_t *new_cwd = nullptr;
  char full_path[256];
  const char *target_path = path;

  if (__builtin_strcmp(path, ".") == 0) {
    new_cwd = proc->p_cwd ? proc->p_cwd : nullptr;
    if (!new_cwd) vfs_lookup("/", &new_cwd);
  } else if (__builtin_strcmp(path, "..") == 0) {
    vfs_lookup("/", &new_cwd);
  } else if (path[0] != '/') {
    const char *cwd_name = "/";
    if (proc->p_cwd) {
      if (proc->p_cwd->v_op && __builtin_strcmp(proc->p_cwd->v_op->vop_name, "fat32_dir") == 0) {
        typedef struct {
          u32 start_cluster;
          u32 file_size;
          bool is_dir;
          char path[256];
        } fat32_path_info_t;
        fat32_path_info_t *nd = (fat32_path_info_t *)proc->p_cwd->v_data;
        if (nd && nd->path[0]) cwd_name = nd->path;
      } else {
        cwd_name = proc->p_cwd->v_name;
      }
    }

    if (__builtin_strcmp(cwd_name, "/") != 0) {
      usize clen = __builtin_strlen(cwd_name);
      __builtin_memcpy(full_path, cwd_name, clen);
      full_path[clen] = '/';
      const char *sub = path;
      if (sub[0] == '.' && sub[1] == '/') sub += 2;
      usize slen = __builtin_strlen(sub);
      if (clen + 1 + slen < sizeof(full_path) - 1) {
        __builtin_memcpy(full_path + clen + 1, sub, slen);
        full_path[clen + 1 + slen] = '\0';
        target_path = full_path;
      }
    } else {
      full_path[0] = '/';
      const char *sub = path;
      if (sub[0] == '.' && sub[1] == '/') sub += 2;
      usize slen = __builtin_strlen(sub);
      if (1 + slen < sizeof(full_path) - 1) {
        __builtin_memcpy(full_path + 1, sub, slen);
        full_path[1 + slen] = '\0';
        target_path = full_path;
      }
    }
  }

  if (!new_cwd) {
    if (vfs_lookup(target_path, &new_cwd) != XIU_SUCCESS || !new_cwd) {
      kprintf("[sys_chdir] path not found: %s (resolved: %s)\n", path, target_path);
      return -1; // enoent
    }
  }

  // verify it's a directory
  if (new_cwd->v_type != VDIR) {
    kprintf("[sys_chdir] not a directory: %s\n", path);
    return -20; // enotdir
  }

  // update CWD
  irq_flags_t irq = spinlock_lock_irqsave(&proc->p_lock);
  vnode_t *old_cwd = proc->p_cwd;
  proc->p_cwd = new_cwd;
  spinlock_unlock_irqrestore(&proc->p_lock, irq);

  dprintf("[sys_chdir] changed CWD to: %s (vnode=%p, name=%s)\n", target_path, new_cwd, new_cwd->v_name);

  (void)old_cwd;
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
        xiu_error_t err = sosend(fp->fp_socket, nullptr, (const void *)buf, len, 0);
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

static i64 sys_lseek(u64 fd_u, u64 offset_u, u64 whence_u, u64 a4, u64 a5, u64 a6) {
  (void)a4; (void)a5; (void)a6;
  int fd = (int)fd_u;
  i64 offset = (i64)offset_u;
  int whence = (int)whence_u;

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || fd < 0 || fd >= XIU_PROC_MAX_FDS) return -9; // ebadf

  xiu_fileproc_t *fp = proc_fd_lookup(proc, fd);
  if (!fp) return -9; // ebadf

  vnode_t *vp = fp->fp_vnode;
  if (!vp) {
    fp_release(fp);
    return -9; // ebadf
  }

  if (vp->v_type == VFIFO || vp->v_type == VSOCK) {
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
  (void)a3; (void)a4; (void)a5; (void)a6;
  int oldfd = (int)oldfd_u;
  int newfd = (int)newfd_u;

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || oldfd < 0 || oldfd >= XIU_PROC_MAX_FDS || newfd < 0 || newfd >= XIU_PROC_MAX_FDS) {
    return -1;
  }
  if (oldfd == newfd) return newfd;

  xiu_fileproc_t *old_fp = proc_fd_lookup(proc, oldfd);
  if (!old_fp) return -1;

  // close existing newfd if open
  proc_fd_close(proc, newfd);

  irq_flags_t irq = spinlock_lock_irqsave(&proc->p_fdlock);
  proc->p_fd_table[newfd] = old_fp;
  spinlock_unlock_irqrestore(&proc->p_fdlock, irq);

  return (i64)newfd;
}

static void resolve_relative_path(xiu_proc_t *proc, const char *path, char *out_buf, usize out_max) {
  if (!path || !out_buf || out_max < 2) return;
  if (path[0] == '/') {
    usize len = __builtin_strlen(path);
    if (len >= out_max) len = out_max - 1;
    __builtin_memcpy(out_buf, path, len);
    out_buf[len] = '\0';
    return;
  }

  const char *cwd_str = "/";
  if (proc && proc->p_cwd) {
    if (proc->p_cwd->v_op && __builtin_strcmp(proc->p_cwd->v_op->vop_name, "fat32_dir") == 0) {
      typedef struct {
        u32 start_cluster;
        u32 file_size;
        bool is_dir;
        char path[256];
      } fat32_path_info_t;
      fat32_path_info_t *nd = (fat32_path_info_t *)proc->p_cwd->v_data;
      if (nd && nd->path[0]) cwd_str = nd->path;
    } else if (proc->p_cwd->v_name[0] == '/') {
      cwd_str = proc->p_cwd->v_name;
    }
  }

  const char *sub = path;
  if (sub[0] == '.' && sub[1] == '/') sub += 2;

  if (__builtin_strcmp(cwd_str, "/") == 0) {
    out_buf[0] = '/';
    usize slen = __builtin_strlen(sub);
    if (1 + slen >= out_max) slen = out_max - 2;
    __builtin_memcpy(out_buf + 1, sub, slen);
    out_buf[1 + slen] = '\0';
  } else {
    usize clen = __builtin_strlen(cwd_str);
    if (clen >= out_max - 2) clen = out_max - 2;
    __builtin_memcpy(out_buf, cwd_str, clen);
    out_buf[clen] = '/';
    usize slen = __builtin_strlen(sub);
    if (clen + 1 + slen >= out_max) slen = out_max - clen - 2;
    __builtin_memcpy(out_buf + clen + 1, sub, slen);
    out_buf[clen + 1 + slen] = '\0';
  }
}

static i64 sys_open(u64 path_ptr, u64 flags, u64 mode, u64 a4, u64 a5, u64 a6) {
  (void)mode;
  (void)a4;
  (void)a5;
  (void)a6;
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

    if (!vp && (flags & 0x0200 || flags & 0x0040 || flags & 0x0100 || (flags & 1) || (flags & 2))) {
      extern xiu_error_t fat32_create_file(const char *path, vnode_t **out_vp);
      fat32_create_file(norm_path, &vp);
    }
  }

  if (!vp) {
    kprintf("[sys_open] '%s': ENOENT (vnode is NULL)\n", path);
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
      extern i64 console_read(char *dst, usize len);
      return console_read((char *)buf, len);
    }
    kprintf("[sys_read] fd=%d: EBADF\n", fd);
    return -1;
  }

  if (fp->fp_type == DTYPE_SOCKET && fp->fp_socket) {
    usize bytes_read = 0;
    xiu_error_t err = soreceive(fp->fp_socket, nullptr, (void *)buf, len, &bytes_read, 0);
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
  if (!pipefd_ptr) return -22; // einval

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc)
    return -1;

  extern xiu_error_t pipe_create(vnode_t **read_vp_out, vnode_t **write_vp_out);
  vnode_t *read_vp = nullptr;
  vnode_t *write_vp = nullptr;

  if (pipe_create(&read_vp, &write_vp) != XIU_SUCCESS || !read_vp || !write_vp) {
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
    if (rfd >= 0) proc_fd_close(proc, rfd);
    if (wfd >= 0) proc_fd_close(proc, wfd);
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
  if (vfs_lookup(normalize_path(path), &elf_vp) != XIU_SUCCESS || !elf_vp) {
    if (path[0] != '/') {
      char bin_path[128];
      bin_path[0] = '/'; bin_path[1] = 'b'; bin_path[2] = 'i'; bin_path[3] = 'n'; bin_path[4] = '/';
      usize plen = __builtin_strlen(path);
      if (plen > 120) plen = 120;
      for (usize i = 0; i < plen; i++) bin_path[5 + i] = path[i];
      bin_path[5 + plen] = '\0';
      vfs_lookup(bin_path, &elf_vp);
    }
  }
  if (!elf_vp) {
    kprintf("[sys_execve] ERROR: vfs_lookup failed for %s\n", path);
    return -1;
  }

  void *elf_ptr = elf_vp->v_data;
  xiu_paddr_t temp_phys = (xiu_paddr_t)-1;
  usize temp_pages = 0;

  if (elf_vp->v_op && __builtin_strcmp(elf_vp->v_op->vop_name, "fat32_file") == 0) {
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
      kprintf("[sys_execve] ERROR: failed to alloc %zu pages for ELF\n", temp_pages);
      return -1;
    }
    extern u64 g_hhdm_base;
    elf_ptr = (void *)(temp_phys + g_hhdm_base);
    u32 actual_read = 0;
    extern xiu_error_t fat32_read_file(u32 start_cluster, u32 file_size, u32 offset, void *dst, u32 len, u32 *bytes_read);
    xiu_error_t err = fat32_read_file(nd->start_cluster, nd->file_size, 0, elf_ptr, nd->file_size, &actual_read);
    if (err != XIU_SUCCESS || actual_read == 0) {
      kprintf("[sys_execve] ERROR: failed to read ELF from disk (err=%d)\n", err);
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
  __asm__ volatile("mov %0, %%cr3" :: "r"(new_pml4) : "memory");

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

  if (!is_anon) {
    // look up the fd in the calling process's file descriptor table
    xiu_task_t *calling_task = current_task();
    xiu_proc_t *calling_proc = calling_task ? calling_task->ta_proc : nullptr;
    if (calling_proc) {
      xiu_fileproc_t *mmap_fp = proc_fd_lookup(calling_proc, (int)fd);
      if (mmap_fp && mmap_fp->fp_vnode) {
        vnode_t *mmap_vp = mmap_fp->fp_vnode;
        // /dev/fb0 has VCHR type and provides vop_mmap
        if (mmap_vp->v_type == VCHR && mmap_vp->v_op &&
            mmap_vp->v_op->vop_mmap != nullptr) {
          is_fb = true;
        }
      }
      if (mmap_fp)
        fp_release(mmap_fp);
    }
    if (!is_fb)
      return -1; // unsupported fd type for mmap
  }

  if (!is_fb && !is_anon)
    return -1;

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

  static u64 s_surface_phys[64][2000];

  for (u64 off = 0; off < len; off += 4096) {
    u64 paddr = 0;
    bool mapped_special = false;

    if (is_fb) {
      paddr = g_fb_phys_addr + off;
      mapped_special = true;
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
  (void)a3; (void)a4; (void)a5; (void)a6;
  if (!addr || !len) return -22; // einval

  xiu_task_t *task = current_task();
  if (!task || !task->ta_vm_map) return -1;

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
extern xiu_error_t ipc_mqueue_send(ipc_port_t *port, ipc_kmsg_t *kmsg,
                                   u32 timeout);
extern xiu_error_t ipc_mqueue_receive(ipc_port_t *port, ipc_kmsg_t **kmsg_out,
                                      u32 timeout);
extern ipc_kmsg_t *ipc_kmsg_alloc(u32 size);
extern void ipc_kmsg_free(ipc_kmsg_t *kmsg);
extern void ipc_port_unlock(ipc_port_t *port);
extern void ipc_port_reference(ipc_port_t *port);
extern mach_port_name_t space_alloc_name(ipc_space_t *space);
extern xiu_error_t mach_register_service(const char *name, ipc_port_t *port);
extern ipc_port_t *mach_lookup_service(const char *name);
extern xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);
extern xiu_error_t copyin(const void *uaddr, void *kaddr, usize len);

static i64 sys_mach_msg(u64 msg_ptr, u64 option, u64 send_sz, u64 rcv_sz,
                        u64 rcv_name, u64 timeout) {
  xiu_task_t *task = current_task();
  if (!task)
    return -1;

  if (option & 1) {
    mach_msg_header_t user_hdr;
    if (copyin((const void *)msg_ptr, &user_hdr, sizeof(user_hdr)) !=
        XIU_SUCCESS)
      return -1;

    if (user_hdr.msgh_id == 1100 && send_sz >= 184) {
      u32 pid = task->ta_proc ? task->ta_proc->p_pid : 0;
      if (copyout(&pid, (void *)(msg_ptr + 180), sizeof(u32)) != XIU_SUCCESS) {
        kprintf("[IPC] Failed to auto-fill client_pid\n");
      }
    }

    ipc_kmsg_t *kmsg = ipc_kmsg_alloc(send_sz);
    if (ipc_kmsg_copyin(kmsg, msg_ptr, task->ta_ipc_space) != XIU_SUCCESS) {
      ipc_kmsg_free(kmsg);
      return -1;
    }

    ipc_port_t *port = kmsg->ikm_remote_port;
    xiu_error_t err = ipc_mqueue_send(port, kmsg, (u32)timeout);

    if (kmsg->ikm_remote_port)
      ipc_port_unlock(kmsg->ikm_remote_port);
    if (kmsg->ikm_local_port && kmsg->ikm_local_port != kmsg->ikm_remote_port)
      ipc_port_unlock(kmsg->ikm_local_port);

    if (err != XIU_SUCCESS) {
      if (err != XIU_ERR_PORT_FULL)
        kprintf("[IPC] Send failed with error %d\n", err);
      ipc_kmsg_free(kmsg);
      return -1;
    }
  }

  if (option & 2) {
    ipc_port_t *port = ipc_port_lookup(
        task->ta_ipc_space, (mach_port_name_t)rcv_name, MACH_PORT_TYPE_RECEIVE);
    if (!port) {
      kprintf("[IPC] RCV port lookup failed\n");
      return -1;
    }

    /*
     * IMPORTANT: ipc_port_lookup returns with port->ip_lock HELD.
     * However, ipc_mqueue_receive may call scheduler_yield().
     * We MUST NOT hold the port lock across a yield, otherwise
     * a sender trying to lock the same port will deadlock the system.
     */
    ipc_port_unlock(port);

    ipc_kmsg_t *kmsg = nullptr;
    xiu_error_t err = ipc_mqueue_receive(port, &kmsg, (u32)timeout);

    if (err != XIU_SUCCESS)
      return -1;

    if (ipc_kmsg_copyout(kmsg, msg_ptr, (u32)rcv_sz, task->ta_ipc_space) !=
        XIU_SUCCESS) {
      ipc_kmsg_free(kmsg);
      return -1;
    }
    ipc_kmsg_free(kmsg);
  }

  return 0;
}

// send IPC message to a specific PID's port — cross-process send
static i64 sys_mach_msg_pid(u64 target_pid, u64 target_port, u64 msg_ptr,
                            u64 send_sz, u64 a5, u64 a6) {
  (void)a5;
  (void)a6;

  xiu_task_t *task = current_task();
  if (!task) {
    kprintf("[IPC] sys_mach_msg_pid: no current task\n");
    return -1;
  }

  // Ищем процесс получателя по PID
  xiu_proc_t *target_proc = proc_find_by_pid((xiu_pid_t)target_pid);
  if (!target_proc || !target_proc->p_task ||
      !target_proc->p_task->ta_ipc_space) {
    kprintf("[IPC] sys_mach_msg_pid: target PID %llu not found\n", target_pid);
    return -1;
  }

  ipc_space_t *target_space = target_proc->p_task->ta_ipc_space;

  // Аллоцируем kmsg для сообщения
  ipc_kmsg_t *kmsg = ipc_kmsg_alloc((mach_msg_size_t)send_sz);
  if (!kmsg)
    return -1;

  /*
   * Копируем сообщение отправителя (WindowServer) и резолвим port-name
   * в IPC-space получателя — так мы сможем отправить сообщение в порт,
   * который видит целевой процесс, но не видит WindowServer.
   */
  xiu_error_t err = ipc_kmsg_copyin(kmsg, msg_ptr, target_space);
  if (err != XIU_SUCCESS) {
    kprintf("[IPC] sys_mach_msg_pid: copyin failed (err=%d)\n", err);
    ipc_kmsg_free(kmsg);
    return -1;
  }

  // Переопределяем PID отправителя
  kmsg->ikm_sender_pid = task->ta_proc ? task->ta_proc->p_pid : 0;

  // Отправляем сообщение в целевой порт
  ipc_port_t *port = kmsg->ikm_remote_port;
  err = ipc_mqueue_send(port, kmsg, 5000);

  // Разблокируем порты
  if (kmsg->ikm_remote_port)
    ipc_port_unlock(kmsg->ikm_remote_port);
  if (kmsg->ikm_local_port && kmsg->ikm_local_port != kmsg->ikm_remote_port)
    ipc_port_unlock(kmsg->ikm_local_port);

  if (err != XIU_SUCCESS) {
    kprintf("[IPC] sys_mach_msg_pid: mqueue_send failed (err=%d)\n", err);
    ipc_kmsg_free(kmsg);
    return -1;
  }

  kprintf("[IPC] sys_mach_msg_pid: delivered to PID=%llu port=%llu\n",
          target_pid, target_port);
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

  ipc_port_t *port = ipc_port_lookup(
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

  ipc_port_t *port = mach_lookup_service(safe_name);
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

static i64 sys_mach_port_deallocate(u64 space_ptr, u64 name, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)space_ptr; (void)a3; (void)a4; (void)a5; (void)a6;
  xiu_task_t *task = current_task();
  if (!task || !task->ta_ipc_space) return -1;
  extern xiu_error_t mach_port_deallocate_kernel(ipc_space_t *space, mach_port_name_t name);
  return (mach_port_deallocate_kernel(task->ta_ipc_space, (mach_port_name_t)name) == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_mach_port_type(u64 space_ptr, u64 name, u64 ptype_out, u64 a4, u64 a5, u64 a6) {
  (void)space_ptr; (void)a4; (void)a5; (void)a6;
  xiu_task_t *task = current_task();
  if (!task || !task->ta_ipc_space) return -1;
  mach_port_type_t ptype = 0;
  extern xiu_error_t mach_port_type_kernel(ipc_space_t *space, mach_port_name_t name, mach_port_type_t *ptype);
  if (mach_port_type_kernel(task->ta_ipc_space, (mach_port_name_t)name, &ptype) != XIU_SUCCESS) return -1;
  if (copyout(&ptype, (void *)ptype_out, sizeof(ptype)) != XIU_SUCCESS) return -1;
  return 0;
}

static i64 sys_task_self(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
  xiu_task_t *task = current_task();
  if (!task || !task->ta_ipc_space) return MACH_PORT_NAME_NULL;
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
  char kpath[128];
  if (copyin((const void *)path, kpath, sizeof(kpath)) != XIU_SUCCESS)
    return -1;
  kpath[sizeof(kpath) - 1] = '\0';

  vnode_t *vp = nullptr;
  if (vfs_lookup(normalize_path(kpath), &vp) != XIU_SUCCESS || !vp)
    return -1;

  xiu_user_stat_t st;
  __builtin_memset(&st, 0, sizeof(st));
  st.st_ino = (u32)((uptr)vp & 0xFFFFFFFFu);
  st.st_mode = vnode_mode(vp);
  st.st_nlink = 1;
  st.st_size = vp->v_attr.va_size;
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
    // fallback to root
    const char cwd[] = "/";
    return (copyout(cwd, (void *)buf, sizeof(cwd)) == XIU_SUCCESS) ? 0 : -1;
  }

  /* For now, we don't have full path tracking in vnodes.
   * We'd need to walk up v_parent chain to reconstruct the full path.
   * As a temporary solution, just return the vnode name or "/" */
  vnode_t *vp = proc->p_cwd;
  const char *path = "/";

  if (vp && vp->v_op && __builtin_strcmp(vp->v_op->vop_name, "fat32_dir") == 0) {
    typedef struct {
      u32 start_cluster;
      u32 file_size;
      bool is_dir;
      char path[256];
    } fat32_path_info_t;
    fat32_path_info_t *nd = (fat32_path_info_t *)vp->v_data;
    if (nd && nd->path[0]) {
      path = nd->path;
    }
  } else if (vp && vp->v_name[0] != '\0') {
    path = vp->v_name;
  }

  usize len = __builtin_strlen(path) + 1;
  if (len > size)
    return -34; // erange
  return (copyout(path, (void *)buf, len) == XIU_SUCCESS) ? 0 : -1;

  // root directory
  return (copyout(path, (void *)buf, 2) == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_mkdir(u64 path_ptr, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)mode; (void)a3; (void)a4; (void)a5; (void)a6;
  if (!path_ptr) return -1;
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
  (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
  if (!path_ptr) return -1;
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
  de.d_off = fp->fp_offset + 1;
  de.d_reclen = sizeof(de);
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
  (void)options;
  (void)rusage;
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
    return -10;
  }

  for (;;) {
    xiu_proc_t *child = proc_find_waitable_child(proc, pid);
    if (child) {
      int status = (int)(child->p_exit_code << 8);
      xiu_pid_t child_pid = child->p_pid;

      extern void proc_reap(xiu_proc_t *proc);
      proc_reap(child);

      extern void console_set_raw_mode(bool raw);
      console_set_raw_mode(false);

      if (status_ptr)
        copyout(&status, (void *)status_ptr, sizeof(status));

      return child_pid;
    }

    /* Re-check if we still have children before yielding.
     * A child might have exited and been reaped by another thread. */
    if (!proc_has_children(proc)) {
      return -10; // echild
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

  // uptime - TODO: implement real uptime tracking
  info.uptime_seconds = 0;

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
  (void)a3; (void)a4; (void)a5; (void)a6;
  xiu_pid_t pid = (xiu_pid_t)pid_u;
  int sig = (int)sig_u;
  if (sig <= 0 || sig >= 32) return -22; // einval

  xiu_proc_t *target = proc_find_by_pid(pid);
  if (!target) return -3; // esrch

  extern xiu_error_t proc_signal(xiu_proc_t *proc, int sig);
  proc_signal(target, sig);
  return 0;
}

struct xiu_sigaction_layout {
  void (*sa_handler)(int);
  u32 sa_mask;
  int sa_flags;
};

static i64 sys_sigaction(u64 signum_u, u64 act_ptr, u64 oldact_ptr, u64 a4, u64 a5, u64 a6) {
  (void)a4; (void)a5; (void)a6;
  int sig = (int)signum_u;
  if (sig <= 0 || sig >= 32 || sig == 9 || sig == 19) {
    return -22;
  }

  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc) return -1;

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

static i64 sys_sigprocmask(u64 how_u, u64 set_ptr, u64 oldset_ptr, u64 a4, u64 a5, u64 a6) {
  (void)a4; (void)a5; (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc) return -1;

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
      if (how == 0) proc->p_sigmask |= set;
      else if (how == 1) proc->p_sigmask &= ~set;
      else if (how == 2) proc->p_sigmask = set;
      spinlock_unlock_irqrestore(&proc->p_lock, irq);
    }
    return 0;
  }

  spinlock_unlock_irqrestore(&proc->p_lock, irq);
  return 0;
}

// socket system calls
static i64 sys_socket(u64 dom, u64 type, u64 proto, u64 a4, u64 a5, u64 a6) {
  (void)a4; (void)a5; (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc) return -1;

  socket_t *so = nullptr;
  xiu_error_t err = socreate((int)dom, &so, (int)type, (int)proto);
  if (err != XIU_SUCCESS || !so) return -1;

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

static i64 sys_bind(u64 fd_u, u64 addr_ptr, u64 addrlen, u64 a4, u64 a5, u64 a6) {
  (void)a4; (void)a5; (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc) return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp) return -1;
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -1; // enotsock
  }

  struct sockaddr_in sin;
  if (addrlen < sizeof(struct sockaddr_in) || copyin((const void *)addr_ptr, &sin, sizeof(sin)) != XIU_SUCCESS) {
    fp_release(fp);
    return -1;
  }

  xiu_error_t err = sobind(fp->fp_socket, (struct sockaddr *)&sin);
  fp_release(fp);
  return (err == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_connect(u64 fd_u, u64 addr_ptr, u64 addrlen, u64 a4, u64 a5, u64 a6) {
  (void)a4; (void)a5; (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc) return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp) return -1;
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -1;
  }

  struct sockaddr_in sin;
  if (addrlen < sizeof(struct sockaddr_in) || copyin((const void *)addr_ptr, &sin, sizeof(sin)) != XIU_SUCCESS) {
    fp_release(fp);
    return -1;
  }

  xiu_error_t err = soconnect(fp->fp_socket, (struct sockaddr *)&sin);
  fp_release(fp);
  return (err == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_listen(u64 fd_u, u64 backlog, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3; (void)a4; (void)a5; (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc) return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp) return -1;
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -1;
  }

  xiu_error_t err = solisten(fp->fp_socket, (int)backlog);
  fp_release(fp);
  return (err == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_accept(u64 fd_u, u64 addr_out, u64 addrlen_out, u64 a4, u64 a5, u64 a6) {
  (void)addr_out; (void)addrlen_out; (void)a4; (void)a5; (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc) return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp) return -1;
  fp_release(fp);
  return -1; // enotsup
}

static i64 sys_sendto(u64 fd_u, u64 buf_ptr, u64 len, u64 flags_u, u64 dest_ptr, u64 addrlen) {
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc) return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp) return -1;
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

static i64 sys_recvfrom(u64 fd_u, u64 buf_ptr, u64 len, u64 flags_u, u64 src_ptr, u64 addrlen_ptr) {
  (void)src_ptr; (void)addrlen_ptr;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc) return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp) return -1;
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -1;
  }

  char kbuf[1500];
  usize clen = (len > sizeof(kbuf)) ? sizeof(kbuf) : len;
  usize bytes_read = 0;
  xiu_error_t err = soreceive(fp->fp_socket, nullptr, kbuf, clen, &bytes_read, (int)flags_u);
  if (err == XIU_SUCCESS && bytes_read > 0) {
    copyout(kbuf, (void *)buf_ptr, bytes_read);
  }
  fp_release(fp);
  return (err == XIU_SUCCESS) ? (i64)bytes_read : -1;
}

static i64 sys_shutdown(u64 fd_u, u64 how, u64 a3, u64 a4, u64 a5, u64 a6) {
  (void)a3; (void)a4; (void)a5; (void)a6;
  xiu_task_t *task = current_task();
  xiu_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc) return -1;

  xiu_fileproc_t *fp = proc_fd_lookup(proc, (int)fd_u);
  if (!fp) return -1;
  if (fp->fp_type != DTYPE_SOCKET || !fp->fp_socket) {
    fp_release(fp);
    return -1;
  }

  xiu_error_t err = soshutdown(fp->fp_socket, (int)how);
  fp_release(fp);
  return (err == XIU_SUCCESS) ? 0 : -1;
}

static i64 sys_setsockopt(u64 fd_u, u64 level, u64 optname, u64 optval, u64 optlen, u64 a6) {
  (void)fd_u; (void)level; (void)optname; (void)optval; (void)optlen; (void)a6;
  return 0;
}

static i64 sys_getsockopt(u64 fd_u, u64 level, u64 optname, u64 optval, u64 optlen_ptr, u64 a6) {
  (void)fd_u; (void)level; (void)optname; (void)optval; (void)optlen_ptr; (void)a6;
  return 0;
}

// syscall table

const syscall_fn_t g_syscall_table[256] = {
    [SYSCALL_LOG] = sys_log,
    [SYS_chdir] = sys_chdir,
    [SYS_fork] = sys_fork,
    [SYS_read] = sys_read,
    [SYS_write] = sys_write,
    [SYS_open] = sys_open,
    [SYS_pipe] = sys_pipe,
    [SYS_close] = sys_close,
    [SYS_wait4] = sys_wait4,
    [SYS_ioctl] = sys_ioctl,
    [18] = sys_stat,
    [SYS_stat] = sys_stat,
    [SYS_getpid] = sys_getpid,

    [SYS_kill] = sys_kill,
    [62] = sys_kill,

    [SYS_sigaction] = sys_sigaction,
    [13] = sys_sigaction,

    [48] = sys_sigprocmask,
    [14] = sys_sigprocmask,

    [SYS_fcntl] = sys_fcntl,
    [90] = sys_dup2,
    [33] = sys_dup2,
    [SYS_execve] = sys_execve,
    [SYS_yield] = sys_yield,
    [SYS_exit] = sys_exit,
    [60] = sys_exit,

    [SYS_getcwd] = sys_getcwd,
    [SYS_mkdir] = sys_mkdir,
    [SYS_rmdir] = sys_rmdir,
    [10] = sys_unlink,
    [87] = sys_unlink,
    [8] = sys_open,
    [199] = sys_lseek,
    [SYS_mmap] = sys_mmap,
    [73] = sys_munmap,
    [11] = sys_munmap,
    [SYS_mach_msg] = sys_mach_msg,
    [201] = sys_mach_port_allocate,
    [202] = sys_mach_msg_pid,
    [203] = sys_mach_register_service,
    [204] = sys_mach_lookup_service,
    [205] = sys_mach_port_deallocate,
    [206] = sys_mach_port_type,
    [208] = sys_task_self,
    [SYS_getdents] = sys_getdents,

    // bsd Darwin Sockets
    [SYS_socket] = sys_socket,
    [SYS_bind] = sys_bind,
    [SYS_connect] = sys_connect,
    [SYS_listen] = sys_listen,
    [SYS_accept] = sys_accept,
    [SYS_sendto] = sys_sendto,
    [SYS_recvfrom] = sys_recvfrom,
    [SYS_shutdown] = sys_shutdown,
    [SYS_setsockopt] = sys_setsockopt,
    [SYS_getsockopt] = sys_getsockopt,

    [SYS_spawn] = sys_spawn,
    [SYS_sysinfo] = sys_sysinfo,
    [SYS_proclist] = sys_proclist,
};

const u32 g_syscall_count = 256;

// dispatcher

i64 syscall_dispatch(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5,
                     u64 arg6, u64 frame) {
  g_syscall_frame = frame;

  if (num >= g_syscall_count || g_syscall_table[num] == nullptr) {
    kprintf("[SYSCALL] Invalid syscall %llu\n", (unsigned long long)num);
    g_syscall_frame = 0;
    return -1;
  }

  i64 ret = g_syscall_table[num](arg1, arg2, arg3, arg4, arg5, arg6);

  if (frame) {
    extern void proc_deliver_signals(void *frame_ptr);
    proc_deliver_signals((void *)frame);
  }

  g_syscall_frame = 0;
  return ret;
}
