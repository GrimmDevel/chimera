/* =============================================================================
 * XIU Operating System — Syscall Numbers for User Space
 * usr/libsystem/include/sys/syscall.h
 * ============================================================================= */

#pragma once
#ifndef SYS_SYSCALL_H
#define SYS_SYSCALL_H

// syscall numbers
#define SYS_log         1
#define SYS_fork        2
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_wait4       7
#define SYS_chdir       12
#define SYS_ioctl       16
#define SYS_stat        18
#define SYS_getpid      20
#define SYS_fcntl       92
#define SYS_execve      59
#define SYS_exit        60
#define SYS_getcwd      79
#define SYS_mkdir       83
#define SYS_rmdir       84
#define SYS_pipe        42
#define SYS_yield       138
#define SYS_mmap        197
#define SYS_mach_msg    200
#define SYS_getdents    217
#define SYS_spawn       250
#define SYS_sysinfo     251

#endif /* SYS_SYSCALL_H */
