// syscall interface
#pragma once
#ifndef XIU_SYSCALL_H
#define XIU_SYSCALL_H

#include <kernel/xiu_types.h>

#define SYSCALL_LOG     253
#define SYS_chdir       12
#define SYS_exit        1

#define SYS_fork        2
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_wait4       7
#define SYS_ioctl       16
#define SYS_stat        18
#define SYS_getpid      20
#define SYS_fcntl       92
#define SYS_execve      59
#define SYS_getcwd      79
#define SYS_mkdir       83
#define SYS_rmdir       84
#define SYS_mmap        197
#define SYS_getdents    217
#define SYS_yield       138
#define SYS_pipe        42
#define SYS_spawn       250
#define SYS_sysinfo     251
#define SYS_proclist    252

#define SYS_mach_msg    200
#define SYS_mach_reply_port 201
#define SYS_mach_msg_pid 202

typedef i64 (*syscall_fn_t)(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6);

extern const syscall_fn_t g_syscall_table[];
extern const u32 g_syscall_count;

i64 syscall_dispatch(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6, u64 frame);

#endif
