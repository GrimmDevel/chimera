/* =============================================================================
 * Chimera Operating System — BSD Personality & Process Management
 * kernel/bsd/proc.c
 * =============================================================================
 */

#include <kernel/fileproc.h>
#include <kernel/ipc_port.h>
#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/vfs_node.h>

chimera_proc_t *proc_kernel = nullptr;
chimera_proc_t *proc_launchd = nullptr;


#define PROC_POOL_SIZE 64
chimera_proc_t s_proc_pool[PROC_POOL_SIZE];
static spinlock_t s_proc_pool_lock = SPINLOCK_INIT;
static _Atomic(u32) s_pid_seq = 1;

chimera_proc_t s_kernel_proc_obj;


void proc_init(void) {
  // task_kernel is already initialized by task_init()
  CHIMERA_ASSERT(task_kernel != nullptr);

  proc_kernel = &s_kernel_proc_obj;
  __builtin_memset(proc_kernel, 0, sizeof(chimera_proc_t));
  proc_kernel->p_signature = CHIMERA_PROC_MAGIC;
  proc_kernel->p_pid = 0;
  proc_kernel->p_task = task_kernel;
  proc_kernel->p_uid = 0;
  proc_kernel->p_euid = 0;
  proc_kernel->p_gid = 0;
  proc_kernel->p_egid = 0;
  proc_kernel->p_groups[0] = 0;
  proc_kernel->p_ngroups = 1;
  proc_kernel->p_umask = 022;
  proc_kernel->p_pgrp = 0;
  proc_kernel->p_sid = 0;
  proc_kernel->p_state = PROC_STATE_RUNNING;
  __builtin_strncpy(proc_kernel->p_comm, "kernel_task", CHIMERA_PROC_NAME_MAX);
  spinlock_init(&proc_kernel->p_lock);
  spinlock_init(&proc_kernel->p_fdlock);

  task_kernel->ta_proc = proc_kernel;
  task_kernel->ta_thread_count = 1;
}

chimera_error_t proc_create(chimera_proc_t *parent, const char *name,
                        chimera_proc_t **proc_out) {
  CHIMERA_ASSERT(proc_out != nullptr);

  irq_flags_t irq = spinlock_lock_irqsave(&s_proc_pool_lock);
  chimera_proc_t *p = nullptr;
  for (u32 i = 1; i < PROC_POOL_SIZE; i++) {
    if (s_proc_pool[i].p_signature != CHIMERA_PROC_MAGIC) {
      p = &s_proc_pool[i];
      break;
    }
  }
  if (!p) {
    spinlock_unlock_irqrestore(&s_proc_pool_lock, irq);
    return CHIMERA_ERR_NOMEM;
  }

  __builtin_memset(p, 0, sizeof(chimera_proc_t));
  p->p_signature = CHIMERA_PROC_MAGIC;
  p->p_pid = atomic_fetch_add(&s_pid_seq, 1);
  if (p->p_pid == 0)
    p->p_pid = atomic_fetch_add(&s_pid_seq, 1);
  p->p_ppid = parent ? parent->p_pid : 0;
  p->p_parent = parent;
  p->p_state = PROC_STATE_RUNNING;
  if (parent) {
    p->p_uid = parent->p_uid;
    p->p_euid = parent->p_euid;
    p->p_svuid = parent->p_svuid;
    p->p_gid = parent->p_gid;
    p->p_egid = parent->p_egid;
    p->p_svgid = parent->p_svgid;
    p->p_ngroups = parent->p_ngroups;
    __builtin_memcpy(p->p_groups, parent->p_groups, sizeof(p->p_groups));
    __builtin_memcpy(p->p_login, parent->p_login, sizeof(p->p_login));
    p->p_umask = parent->p_umask;
    p->p_pgrp = parent->p_pgrp ? parent->p_pgrp : p->p_pid;
    p->p_sid = parent->p_sid ? parent->p_sid : p->p_pid;
    p->p_tty = parent->p_tty;
  } else {
    p->p_uid = 0;
    p->p_euid = 0;
    p->p_svuid = 0;
    p->p_gid = 0;
    p->p_egid = 0;
    p->p_svgid = 0;
    p->p_ngroups = 1;
    p->p_groups[0] = 0;
    p->p_umask = 022;
    p->p_pgrp = p->p_pid;
    p->p_sid = p->p_pid;
  }
  __builtin_strncpy(p->p_comm, name, CHIMERA_PROC_NAME_MAX);
  spinlock_init(&p->p_lock);
  spinlock_init(&p->p_fdlock);
  spinlock_unlock_irqrestore(&s_proc_pool_lock, irq);

  // create associated Mach task
  chimera_error_t err = task_create(parent ? parent->p_task : nullptr, &p->p_task);
  if (CHIMERA_FAILED(err))
    return err;

  p->p_task->ta_proc = p;

  // cwd setup
  if (parent && parent->p_cwd) {
    p->p_cwd = parent->p_cwd;
  } else {
    extern chimera_error_t vfs_lookup(const char *path, vnode_t **vp_out);
    vnode_t *root_vnode = nullptr;
    if (vfs_lookup("/", &root_vnode) == CHIMERA_SUCCESS && root_vnode) {
      p->p_cwd = root_vnode;
    }
  }

  bool has_parent_fds = false;
  if (parent) {
    irq_flags_t pirq = spinlock_lock_irqsave(&parent->p_fdlock);
    for (int i = 0; i < CHIMERA_PROC_MAX_FDS; i++) {
      if (parent->p_fd_table[i]) {
        has_parent_fds = true;
        break;
      }
    }
    if (has_parent_fds) {
      irq_flags_t cirq = spinlock_lock_irqsave(&p->p_fdlock);
      for (int i = 0; i < CHIMERA_PROC_MAX_FDS; i++) {
        chimera_fileproc_t *fp = parent->p_fd_table[i];
        if (fp) {
          fp_retain(fp);
          p->p_fd_table[i] = fp;
          p->p_fd_flags[i] = parent->p_fd_flags[i];
        }
      }
      spinlock_unlock_irqrestore(&p->p_fdlock, cirq);
    }
    spinlock_unlock_irqrestore(&parent->p_fdlock, pirq);
  }

  if (!has_parent_fds) {
    extern chimera_error_t vfs_lookup(const char *path, vnode_t **vp_out);
    extern chimera_fileproc_t *fp_alloc(vnode_t * vp, u32 flags);
    extern int proc_fd_install(chimera_proc_t * p, chimera_fileproc_t * fp);
    extern void fp_release(chimera_fileproc_t * fp);

    vnode_t *dev_con = nullptr;
    if (vfs_lookup("/dev/console", &dev_con) != CHIMERA_SUCCESS || !dev_con) {
      vfs_lookup("/dev/null", &dev_con);
    }
    if (dev_con) {
      // fd 0: stdin
      chimera_fileproc_t *fp0 = fp_alloc(dev_con, FP_READABLE);
      if (fp0) {
        proc_fd_install(p, fp0);
        fp_release(fp0);
      }

      // fd 1: stdout
      chimera_fileproc_t *fp1 = fp_alloc(dev_con, FP_WRITABLE);
      if (fp1) {
        proc_fd_install(p, fp1);
        fp_release(fp1);
      }

      // fd 2: stderr
      chimera_fileproc_t *fp2 = fp_alloc(dev_con, FP_WRITABLE);
      if (fp2) {
        proc_fd_install(p, fp2);
        fp_release(fp2);
      }
    }
  }

  // inherit signal state from parent
  if (parent) {
    p->p_sigmask = parent->p_sigmask;
    __builtin_memcpy(p->p_sigacts, parent->p_sigacts, sizeof(p->p_sigacts));
    __builtin_memcpy(p->p_sigact_flags, parent->p_sigact_flags,
                     sizeof(p->p_sigact_flags));
    __builtin_memcpy(p->p_sigact_mask, parent->p_sigact_mask,
                     sizeof(p->p_sigact_mask));
  }
  p->p_sigpending = 0;

  *proc_out = p;
  return CHIMERA_SUCCESS;
}

chimera_proc_t *proc_find_by_pid(chimera_pid_t pid) {
  irq_flags_t irq = spinlock_lock_irqsave(&s_proc_pool_lock);
  for (u32 i = 0; i < PROC_POOL_SIZE; i++) {
    chimera_proc_t *p = &s_proc_pool[i];
    if (p->p_signature == CHIMERA_PROC_MAGIC && p->p_pid == pid) {
      spinlock_unlock_irqrestore(&s_proc_pool_lock, irq);
      return p;
    }
  }
  spinlock_unlock_irqrestore(&s_proc_pool_lock, irq);
  return nullptr;
}

chimera_proc_t *proc_find_by_name(const char *name) {
  if (!name)
    return nullptr;
  irq_flags_t irq = spinlock_lock_irqsave(&s_proc_pool_lock);
  for (u32 i = 0; i < PROC_POOL_SIZE; i++) {
    chimera_proc_t *p = &s_proc_pool[i];
    if (p->p_signature == CHIMERA_PROC_MAGIC &&
        __builtin_strcmp(p->p_comm, name) == 0) {
      spinlock_unlock_irqrestore(&s_proc_pool_lock, irq);
      return p;
    }
  }
  spinlock_unlock_irqrestore(&s_proc_pool_lock, irq);
  return nullptr;
}

chimera_proc_t *proc_find_by_pgrp(chimera_pid_t pgrp) {
  for (u32 i = 0; i < PROC_POOL_SIZE; i++) {
    chimera_proc_t *p = &s_proc_pool[i];
    if (p->p_signature == CHIMERA_PROC_MAGIC && p->p_pgrp == pgrp)
      return p;
  }
  return nullptr;
}

void proc_mark_exited(chimera_proc_t *proc, u32 code) {
  if (!proc)
    return;

  // 1. Close all open file descriptors
  irq_flags_t fd_irq = spinlock_lock_irqsave(&proc->p_fdlock);
  for (int i = 0; i < CHIMERA_PROC_MAX_FDS; i++) {
    chimera_fileproc_t *fp = proc->p_fd_table[i];
    if (fp) {
      proc->p_fd_table[i] = nullptr;
      fp_release(fp);
    }
  }
  spinlock_unlock_irqrestore(&proc->p_fdlock, fd_irq);

  // 2. Release vnode references
  proc->p_text_node = nullptr;
  proc->p_cwd = nullptr;

  // 3. Halt and remove all threads BEFORE destroying address space
  if (proc->p_task) {
    chimera_thread_t *th = proc->p_task->ta_threads;
    while (th) {
      chimera_thread_t *next = th->th_task_next;
      th->th_state = THREAD_STATE_HALTED;
      scheduler_remove_thread(th);
      th->th_signature = 0;
      th = next;
    }
    proc->p_task->ta_threads = nullptr;
    proc->p_task->ta_thread_count = 0;

    if (proc->p_task->ta_vm_map) {
      extern void pmap_destroy_user_space(u64 pml4_phys);
      pmap_destroy_user_space((u64)proc->p_task->ta_vm_map);
      proc->p_task->ta_vm_map = nullptr;
    }

    if (proc->p_task->ta_ipc_space) {
      extern void ipc_space_destroy(ipc_space_t * space);
      ipc_space_destroy(proc->p_task->ta_ipc_space);
      proc->p_task->ta_ipc_space = nullptr;
    }
  }

  // 4. Reparent orphan children to launchd (PID 1)
  irq_flags_t pool_irq = spinlock_lock_irqsave(&s_proc_pool_lock);
  for (u32 i = 1; i < PROC_POOL_SIZE; i++) {
    chimera_proc_t *child = &s_proc_pool[i];
    if (child->p_signature == CHIMERA_PROC_MAGIC &&
        (child->p_parent == proc || child->p_ppid == proc->p_pid)) {
      child->p_parent = proc_launchd;
      child->p_ppid = (proc_launchd ? proc_launchd->p_pid : 1);
    }
  }
  spinlock_unlock_irqrestore(&s_proc_pool_lock, pool_irq);

  // 5. Set state to EXITED
  irq_flags_t irq = spinlock_lock_irqsave(&proc->p_lock);
  proc->p_exit_code = code;
  proc->p_state = PROC_STATE_EXITED;
  spinlock_unlock_irqrestore(&proc->p_lock, irq);

  // 6. Send SIGCHLD (20 in Darwin/BSD) to parent if present
  if (proc->p_parent) {
    proc_signal(proc->p_parent, 20);
  }
}

void proc_reap(chimera_proc_t *proc) {
  if (!proc || proc->p_state != PROC_STATE_EXITED)
    return;
  irq_flags_t irq = spinlock_lock_irqsave(&proc->p_lock);

  if (proc->p_task) {
    if (proc->p_task->ta_vm_map) {
      extern void pmap_destroy_user_space(u64 pml4_phys);
      pmap_destroy_user_space((u64)proc->p_task->ta_vm_map);
      proc->p_task->ta_vm_map = nullptr;
    }
    proc->p_task->ta_signature = 0;
    proc->p_task = nullptr;
  }

  spinlock_unlock_irqrestore(&proc->p_lock, irq);

  // Clear process slot for reuse under global pool lock
  irq_flags_t pool_irq = spinlock_lock_irqsave(&s_proc_pool_lock);
  proc->p_signature = 0;
  proc->p_state = 0;
  proc->p_pid = 0;
  proc->p_ppid = 0;
  proc->p_parent = nullptr;
  spinlock_unlock_irqrestore(&s_proc_pool_lock, pool_irq);
}

chimera_error_t proc_signal(chimera_proc_t *proc, int sig) {
  if (!proc || sig <= 0 || sig >= 32)
    return CHIMERA_ERR_INVALID;
  irq_flags_t irq = spinlock_lock_irqsave(&proc->p_lock);

  if (sig == 9) {
    proc->p_sigpending |= (1U << sig);
    spinlock_unlock_irqrestore(&proc->p_lock, irq);
    if (proc->p_task && proc->p_task->ta_threads) {
      thread_wake(proc->p_task->ta_threads);
    }
    return CHIMERA_SUCCESS;
  }

  u64 act = proc->p_sigacts[sig];
  if (act == 1) {
    spinlock_unlock_irqrestore(&proc->p_lock, irq);
    return CHIMERA_SUCCESS;
  }

  proc->p_sigpending |= (1U << sig);
  spinlock_unlock_irqrestore(&proc->p_lock, irq);

  if (proc->p_task && proc->p_task->ta_threads) {
    thread_wake(proc->p_task->ta_threads);
  }
  return CHIMERA_SUCCESS;
}

typedef struct syscall_user_frame_alias {
  u64 r15, r14, r13, r12, rbx, rbp;
  u64 rip;
  u64 rflags;
  u64 rsp;
} syscall_user_frame_alias_t;

extern i64 sys_exit_internal(u64 code);

void proc_deliver_signals(void *frame_ptr) {
  chimera_task_t *task = current_task();
  chimera_proc_t *proc = task ? task->ta_proc : nullptr;
  if (!proc || proc->p_pid == 0 || !frame_ptr)
    return;

  irq_flags_t irq = spinlock_lock_irqsave(&proc->p_lock);
  u32 deliverable = proc->p_sigpending & ~proc->p_sigmask;
  if (!deliverable) {
    spinlock_unlock_irqrestore(&proc->p_lock, irq);
    return;
  }

  for (int sig = 1; sig < 32; sig++) {
    if (deliverable & (1U << sig)) {
      proc->p_sigpending &= ~(1U << sig);
      u64 handler = proc->p_sigacts[sig];
      spinlock_unlock_irqrestore(&proc->p_lock, irq);

      if (handler == 1 || sig == 20 || sig == 28 || sig == 16 || sig == 29) {
        return;
      }

      if (handler == 0) {
        kprintf("[SIGNAL] Process '%s' (PID %u) terminated by signal %d\n",
                proc->p_comm, proc->p_pid, sig);
        extern void sys_exit_direct(u64 code);
        sys_exit_direct(128 + sig);
        return;
      }
      return;
    }
  }
  spinlock_unlock_irqrestore(&proc->p_lock, irq);
}

chimera_proc_t *proc_find_waitable_child(chimera_proc_t *parent, chimera_pid_t pid) {
  if (!parent)
    return nullptr;
  for (u32 i = 1; i < PROC_POOL_SIZE; i++) {
    chimera_proc_t *p = &s_proc_pool[i];
    if (p->p_signature != CHIMERA_PROC_MAGIC)
      continue;
    if (p->p_parent != parent && p->p_ppid != parent->p_pid)
      continue;
    if (pid != (chimera_pid_t)-1 && pid != 0 && p->p_pid != pid)
      continue;
    if (p->p_state == PROC_STATE_EXITED)
      return p;
  }
  return nullptr;
}

int proc_has_children(chimera_proc_t *parent) {
  if (!parent)
    return 0;
  for (u32 i = 1; i < PROC_POOL_SIZE; i++) {
    chimera_proc_t *p = &s_proc_pool[i];
    if (p->p_signature != CHIMERA_PROC_MAGIC)
      continue;
    if (p->p_parent == parent || p->p_ppid == parent->p_pid)
      return 1;
  }
  return 0;
}

chimera_error_t task_create(chimera_task_t *parent, chimera_task_t **task_out) {
  CHIMERA_ASSERT(task_out != nullptr);
  (void)parent;

  static chimera_task_t s_task_pool[64];
  static spinlock_t s_task_pool_lock = SPINLOCK_INIT;

  irq_flags_t irq = spinlock_lock_irqsave(&s_task_pool_lock);
  chimera_task_t *t = nullptr;
  u32 idx = 0;
  for (u32 i = 1; i < 64; i++) {
    if (s_task_pool[i].ta_signature != CHIMERA_TASK_MAGIC) {
      t = &s_task_pool[i];
      idx = i;
      break;
    }
  }
  if (!t) {
    spinlock_unlock_irqrestore(&s_task_pool_lock, irq);
    return CHIMERA_ERR_NOMEM;
  }

  __builtin_memset(t, 0, sizeof(chimera_task_t));
  t->ta_signature = CHIMERA_TASK_MAGIC;
  t->ta_id = idx;
  t->ta_thread_count = 1;
  spinlock_init(&t->ta_lock);
  spinlock_unlock_irqrestore(&s_task_pool_lock, irq);

  extern u64 pmap_create(void);
  u64 pml4_phys = pmap_create();
  t->ta_vm_map = (void *)pml4_phys;

  chimera_error_t err = ipc_space_create(t, &t->ta_ipc_space);
  if (CHIMERA_FAILED(err))
    return err;

  mach_port_name_t tp_name;
  err = ipc_port_alloc(t->ta_ipc_space, &tp_name, "task.self");
  if (CHIMERA_SUCCEEDED(err)) {
    struct ipc_port *tp = t->ta_ipc_space->is_table[tp_name].ie_object;
    if (tp) {
      tp->ip_kobject = t;
      tp->ip_kotype = 1;
    }
    t->ta_task_port = tp_name;
  }

  *task_out = t;
  return CHIMERA_SUCCESS;
}
chimera_task_t *current_task(void) {
  chimera_thread_t *th = current_thread();
  return th ? th->th_task : nullptr;
}
