// xnu bsd syscall numbers
#ifndef XIU_BSD_SYSCALL_XNU_H
#define XIU_BSD_SYSCALL_XNU_H

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_syscall         0
#define SYS_exit            1
#define SYS_fork            2
#define SYS_read            3
#define SYS_write           4
#define SYS_open            5
#define SYS_close           6
#define SYS_wait4           7
#define SYS_link            9
#define SYS_unlink          10
#define SYS_chdir           12
#define SYS_mknod           14
#define SYS_chmod           15
#define SYS_chown           16
#define SYS_getpid          20
#define SYS_setuid          23
#define SYS_getuid          24
#define SYS_geteuid         25
#define SYS_recvfrom        29
#define SYS_accept          30
#define SYS_getpeername     31
#define SYS_getsockname     32
#define SYS_kill            37
#define SYS_getppid         39
#define SYS_dup             41
#define SYS_pipe            42
#define SYS_getegid         43
#define SYS_sigaction       46
#define SYS_ioctl           54
#define SYS_reboot          55
#define SYS_execve          59
#define SYS_umask           60
#define SYS_munmap          73
#define SYS_mprotect        74
#define SYS_madvise         75
#define SYS_fcntl           92
#define SYS_fsync           95
#define SYS_socket          97
#define SYS_connect         98
#define SYS_bind            104
#define SYS_setsockopt      105
#define SYS_listen          106
#define SYS_getsockopt      118
#define SYS_readv           120
#define SYS_writev          121
#define SYS_rename          128
#define SYS_flock           131
#define SYS_sendto          133
#define SYS_shutdown        134
#define SYS_mkdir           136
#define SYS_rmdir           137
#define SYS_utimes          138
#define SYS_stat            188
#define SYS_fstat           189
#define SYS_lstat           190
#define SYS_mmap            197
#define SYS_lseek           199
#define SYS_truncate        200
#define SYS_ftruncate       201
#define SYS_sysctl          202
#define SYS_posix_spawn     244

#define SYS_mach_msg_trap   31
#define SYS_mach_reply_port 26

#ifdef __cplusplus
}
#endif

#endif
