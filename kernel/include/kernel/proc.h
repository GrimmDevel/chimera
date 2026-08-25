// processes and threads (bsd personality + mach task/threads)
#pragma once
#ifndef XIU_PROC_H
#define XIU_PROC_H

#include <kernel/xiu_types.h>
#include <kernel/spinlock.h>
#include <kernel/ipc_port.h>
#include <kernel/fileproc.h>

typedef u32 thread_state_t;
#define THREAD_STATE_READY      1
#define THREAD_STATE_STOPPED    2
#define THREAD_STATE_WAITING    3
#define THREAD_STATE_HALTED     4
#define THREAD_STATE_RUNNING    5

typedef struct xiu_thread {
    u64                 th_signature;
    u64                 th_id;
    struct xiu_task    *th_task;
    
    thread_state_t      th_state;
    u32                 th_priority;
    
    void               *th_context;
    void               *th_saved_sp;
    void               *th_kernel_stack;
    void               *th_user_stack;
    void               *th_user_frame;
    void               *th_stack_base;
    usize               th_stack_size;

    u32                 th_base_priority;
    u32                 th_sched_priority;
    u32                 th_cpu_usage;
    u32                 th_running_cpu;

    u32                 th_is_fork_child;
    u64                 th_fork_return_value;
    u64                 th_fork_frame[9];

    ipc_port_t         *th_reply_port;
    xiu_error_t         th_wait_result;
    
    struct xiu_thread  *th_next;
    struct xiu_thread  *th_task_next;
    struct xiu_thread  *th_wait_next;

    u8                  th_fp_state[512] __attribute__((aligned(64)));
    u32                 th_fp_initialized;
} xiu_thread_t;

#define XIU_THREAD_MAGIC 0x5448524541442121ULL

typedef struct xiu_task {
    u64                 ta_signature;
    u64                 ta_id;
    u32                 ta_flags;
    
    struct xiu_proc    *ta_proc;
    struct ipc_space   *ta_ipc_space;
    void               *ta_vm_map;
    u64                 ta_mmap_next;
    mach_port_name_t    ta_task_port;
    mach_port_name_t    ta_bootstrap_port;
    
    xiu_thread_t       *ta_threads;
    u32                 ta_thread_count;
    
    spinlock_t          ta_lock;
} xiu_task_t;

#define XIU_TASK_MAGIC 0x5441534b21212121ULL

#define TASK_FLAG_KERNEL    (1u << 0)
#define TASK_FLAG_64BIT     (1u << 1)

#define XIU_PROC_NAME_MAX   32
#define XIU_PROC_MAX_FDS    256

#define PROC_STATE_RUNNING  1
#define PROC_STATE_EXITED   2

typedef struct xiu_proc {
    u64                 p_signature;
    xiu_pid_t           p_pid;
    xiu_pid_t           p_ppid;
    struct xiu_proc    *p_parent;
    
    char                p_comm[XIU_PROC_NAME_MAX];
    char                p_login[32];
    
    struct xiu_task    *p_task;
    xiu_uid_t           p_uid;
    xiu_uid_t           p_euid;
    xiu_gid_t           p_gid;
    xiu_gid_t           p_egid;
    xiu_gid_t           p_groups[16];
    u32                 p_ngroups;

    xiu_pid_t           p_pgrp;
    xiu_pid_t           p_sid;
    void               *p_tty;

    u32                 p_umask;

    struct vnode       *p_text_node;
    struct vnode       *p_cwd;

    xiu_fileproc_t     *p_fd_table[XIU_PROC_MAX_FDS];
    u8                  p_fd_flags[XIU_PROC_MAX_FDS];
    spinlock_t          p_fdlock;

    u32                 p_exit_code;
    u32                 p_state;
    spinlock_t          p_lock;

    u32                 p_sigmask;
    u32                 p_sigpending;
    u64                 p_sigacts[32];
    u32                 p_sigact_flags[32];
    u32                 p_sigact_mask[32];
} xiu_proc_t;

#define XIU_PROC_MAGIC 0x50524F4321212121ULL

typedef struct cpu_local {
    xiu_thread_t       *cpu_current_thread;  // offset 0x00
    void               *cpu_user_rsp_save;   // offset 0x08
    u32                 cpu_id;              // offset 0x10
    u32                 cpu_lapic_id;        // offset 0x14
    u32                 cpu_is_bsp;          // offset 0x18
    u32                 cpu_is_active;       // offset 0x1C
    void               *cpu_kernel_stack;    // offset 0x20
    xiu_thread_t       *cpu_idle_thread;     // offset 0x28
    void               *cpu_gdt_ptr;         // offset 0x30
    void               *cpu_tss_ptr;         // offset 0x38
} cpu_local_t;

#define CPU_LOCAL_CURRENT_THREAD    0x0
#define CPU_LOCAL_USER_RSP_SAVE     0x8
#define THREAD_KERNEL_STACK_OFFSET  0x30

extern xiu_proc_t  *proc_kernel;
extern xiu_proc_t  *proc_launchd;
extern xiu_task_t  *task_kernel;

extern cpu_local_t  g_cpu_data[16];
#define cpu_local_bsp g_cpu_data[0]

void proc_init(void);
xiu_error_t proc_create(xiu_proc_t *parent, const char *name, xiu_proc_t **proc_out);
xiu_error_t task_create(xiu_task_t *parent, xiu_task_t **task_out);
void proc_mark_exited(xiu_proc_t *proc, u32 code);
xiu_proc_t *proc_find_by_pid(xiu_pid_t pid);
xiu_proc_t *proc_find_waitable_child(xiu_proc_t *parent, xiu_pid_t pid);
xiu_error_t proc_signal(xiu_proc_t *proc, int sig);
void proc_deliver_signals(void *frame);

void thread_wake(xiu_thread_t *thread);
void scheduler_remove_thread(xiu_thread_t *th);

xiu_thread_t *current_thread(void);
xiu_task_t *current_task(void);

xiu_error_t copyin(const void *uaddr, void *kaddr, usize len);
xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);
xiu_error_t copyinstr(const void *uaddr, char *kaddr, usize maxlen, usize *lencopied);

#endif
