// processes and threads (bsd personality + mach task/threads)
#pragma once
#ifndef CHIMERA_PROC_H
#define CHIMERA_PROC_H

#include <kernel/chimera_types.h>
#include <kernel/spinlock.h>
#include <kernel/ipc_port.h>
#include <kernel/fileproc.h>

typedef u32 thread_state_t;
#define THREAD_STATE_READY      1
#define THREAD_STATE_STOPPED    2
#define THREAD_STATE_WAITING    3
#define THREAD_STATE_HALTED     4
#define THREAD_STATE_RUNNING    5

typedef struct chimera_thread {
    u64                 th_signature;
    u64                 th_id;
    struct chimera_task    *th_task;
    
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
    chimera_error_t         th_wait_result;
    
    struct chimera_thread  *th_next;
    struct chimera_thread  *th_task_next;
    struct chimera_thread  *th_wait_next;
} chimera_thread_t;

#define CHIMERA_THREAD_MAGIC 0x5448524541442121ULL

typedef struct chimera_task {
    u64                 ta_signature;
    u64                 ta_id;
    u32                 ta_flags;
    
    struct chimera_proc    *ta_proc;
    struct ipc_space   *ta_ipc_space;
    void               *ta_vm_map;
    u64                 ta_mmap_next;
    mach_port_name_t    ta_task_port;
    mach_port_name_t    ta_bootstrap_port;
    
    chimera_thread_t       *ta_threads;
    u32                 ta_thread_count;
    
    spinlock_t          ta_lock;
} chimera_task_t;

#define CHIMERA_TASK_MAGIC 0x5441534b21212121ULL

#define TASK_FLAG_KERNEL    (1u << 0)
#define TASK_FLAG_64BIT     (1u << 1)

#define CHIMERA_PROC_NAME_MAX   32
#define CHIMERA_PROC_MAX_FDS    256

#define PROC_STATE_RUNNING  1
#define PROC_STATE_EXITED   2

typedef struct chimera_proc {
    u64                 p_signature;
    chimera_pid_t           p_pid;
    chimera_pid_t           p_ppid;
    struct chimera_proc    *p_parent;
    
    char                p_comm[CHIMERA_PROC_NAME_MAX];
    char                p_login[32];
    
    struct chimera_task    *p_task;
    chimera_uid_t           p_uid;
    chimera_uid_t           p_euid;
    chimera_gid_t           p_gid;
    chimera_gid_t           p_egid;
    chimera_gid_t           p_groups[16];
    u32                 p_ngroups;

    chimera_pid_t           p_pgrp;
    chimera_pid_t           p_sid;
    void               *p_tty;

    u32                 p_umask;

    struct vnode       *p_text_node;
    struct vnode       *p_cwd;

    chimera_fileproc_t     *p_fd_table[CHIMERA_PROC_MAX_FDS];
    u8                  p_fd_flags[CHIMERA_PROC_MAX_FDS];
    spinlock_t          p_fdlock;

    u32                 p_exit_code;
    u32                 p_state;
    spinlock_t          p_lock;

    u32                 p_sigmask;
    u32                 p_sigpending;
    u64                 p_sigacts[32];
    u32                 p_sigact_flags[32];
    u32                 p_sigact_mask[32];
} chimera_proc_t;

#define CHIMERA_PROC_MAGIC 0x50524F4321212121ULL

typedef struct cpu_local {
    chimera_thread_t       *cpu_current_thread;  // offset 0x00
    void               *cpu_user_rsp_save;   // offset 0x08
    u32                 cpu_id;              // offset 0x10
    u32                 cpu_lapic_id;        // offset 0x14
    u32                 cpu_is_bsp;          // offset 0x18
    u32                 cpu_is_active;       // offset 0x1C
    void               *cpu_kernel_stack;    // offset 0x20
    chimera_thread_t       *cpu_idle_thread;     // offset 0x28
    void               *cpu_gdt_ptr;         // offset 0x30
    void               *cpu_tss_ptr;         // offset 0x38
} cpu_local_t;

#define CPU_LOCAL_CURRENT_THREAD    0x0
#define CPU_LOCAL_USER_RSP_SAVE     0x8
#define THREAD_KERNEL_STACK_OFFSET  0x30

extern chimera_proc_t  *proc_kernel;
extern chimera_proc_t  *proc_launchd;
extern chimera_task_t  *task_kernel;

extern cpu_local_t  g_cpu_data[16];
#define cpu_local_bsp g_cpu_data[0]

void proc_init(void);
chimera_error_t proc_create(chimera_proc_t *parent, const char *name, chimera_proc_t **proc_out);
chimera_error_t task_create(chimera_task_t *parent, chimera_task_t **task_out);
void proc_mark_exited(chimera_proc_t *proc, u32 code);
chimera_proc_t *proc_find_by_pid(chimera_pid_t pid);
chimera_proc_t *proc_find_waitable_child(chimera_proc_t *parent, chimera_pid_t pid);
chimera_error_t proc_signal(chimera_proc_t *proc, int sig);
void proc_deliver_signals(void *frame);

void thread_wake(chimera_thread_t *thread);
void scheduler_remove_thread(chimera_thread_t *th);

chimera_thread_t *current_thread(void);
chimera_task_t *current_task(void);

chimera_error_t copyin(const void *uaddr, void *kaddr, usize len);
chimera_error_t copyout(const void *kaddr, void *uaddr, usize len);
chimera_error_t copyinstr(const void *uaddr, char *kaddr, usize maxlen, usize *lencopied);

#endif
