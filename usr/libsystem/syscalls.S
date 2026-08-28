/* =============================================================================
 * Chimera Operating System — User-Space Syscall Stubs (Mach-O Darwin ABI)
 * usr/libsystem/syscalls.S
 * ============================================================================= */

#define SYSCALL_STUB(name, num) \
    .globl _##name;             \
    _##name:                    \
        movq $num, %rax;        \
        movq %rcx, %r10;        \
        syscall;                \
        retq;

.text

SYSCALL_STUB(chimera_log, 253)

SYSCALL_STUB(chimera_exit, 1)
SYSCALL_STUB(chimera_fork, 2)
SYSCALL_STUB(chimera_read, 3)
SYSCALL_STUB(chimera_write, 4)
SYSCALL_STUB(chimera_open, 5)
SYSCALL_STUB(chimera_close, 6)
SYSCALL_STUB(chimera_wait4, 7)
SYSCALL_STUB(chimera_unlink, 10)
SYSCALL_STUB(chimera_chdir, 12)
SYSCALL_STUB(chimera_fchdir, 13)
SYSCALL_STUB(chimera_chmod, 15)
SYSCALL_STUB(chimera_chown, 16)
SYSCALL_STUB(chimera_getpid, 20)
SYSCALL_STUB(chimera_setuid, 23)
SYSCALL_STUB(chimera_getuid, 24)
SYSCALL_STUB(chimera_geteuid, 25)
SYSCALL_STUB(chimera_recvfrom, 29)
SYSCALL_STUB(chimera_accept, 30)
SYSCALL_STUB(chimera_getsockname, 32)
SYSCALL_STUB(chimera_access, 33)
SYSCALL_STUB(chimera_kill, 37)
SYSCALL_STUB(chimera_getppid, 39)
SYSCALL_STUB(chimera_dup, 41)
SYSCALL_STUB(chimera_pipe, 42)
SYSCALL_STUB(chimera_getegid, 43)
SYSCALL_STUB(chimera_sigaction, 46)
SYSCALL_STUB(chimera_getgid, 47)
SYSCALL_STUB(chimera_sigprocmask, 48)
SYSCALL_STUB(chimera_getlogin, 49)
SYSCALL_STUB(chimera_setlogin, 50)
SYSCALL_STUB(chimera_sigpending, 52)
SYSCALL_STUB(chimera_sigaltstack, 53)
SYSCALL_STUB(chimera_ioctl, 54)
SYSCALL_STUB(chimera_execve, 59)
SYSCALL_STUB(chimera_umask, 60)
SYSCALL_STUB(chimera_munmap, 73)
SYSCALL_STUB(chimera_mprotect, 74)
SYSCALL_STUB(chimera_getgroups, 79)
SYSCALL_STUB(chimera_setgroups, 80)
SYSCALL_STUB(chimera_getpgrp, 81)
SYSCALL_STUB(chimera_setpgid, 82)
SYSCALL_STUB(chimera_dup2, 90)
SYSCALL_STUB(chimera_fcntl, 92)
SYSCALL_STUB(chimera_select, 93)
SYSCALL_STUB(chimera_fsync, 95)
SYSCALL_STUB(chimera_socket, 97)
SYSCALL_STUB(chimera_connect, 98)
SYSCALL_STUB(chimera_bind, 104)
SYSCALL_STUB(chimera_setsockopt, 105)
SYSCALL_STUB(chimera_listen, 106)
SYSCALL_STUB(chimera_gettimeofday, 116)
SYSCALL_STUB(chimera_getsockopt, 118)
SYSCALL_STUB(chimera_readv, 120)
SYSCALL_STUB(chimera_writev, 121)
SYSCALL_STUB(chimera_settimeofday, 122)
SYSCALL_STUB(chimera_fchown, 123)
SYSCALL_STUB(chimera_fchmod, 124)
SYSCALL_STUB(chimera_rename, 128)
SYSCALL_STUB(chimera_sendto, 133)
SYSCALL_STUB(chimera_shutdown, 134)
SYSCALL_STUB(chimera_socketpair, 135)
SYSCALL_STUB(chimera_mkdir, 136)
SYSCALL_STUB(chimera_rmdir, 137)
SYSCALL_STUB(chimera_setsid, 147)
SYSCALL_STUB(chimera_getpgid, 151)
SYSCALL_STUB(chimera_pread, 153)
SYSCALL_STUB(chimera_pwrite, 154)
SYSCALL_STUB(chimera_setgid, 181)
SYSCALL_STUB(chimera_setegid, 182)
SYSCALL_STUB(chimera_seteuid, 183)
SYSCALL_STUB(chimera_getsid, 310)
SYSCALL_STUB(chimera_stat, 188)
SYSCALL_STUB(chimera_fstat, 189)
SYSCALL_STUB(chimera_lstat, 190)
SYSCALL_STUB(chimera_getdents, 196)
SYSCALL_STUB(chimera_mmap, 197)
SYSCALL_STUB(chimera_lseek, 199)
SYSCALL_STUB(chimera_truncate, 200)
SYSCALL_STUB(chimera_ftruncate, 201)
SYSCALL_STUB(chimera_sysctl, 202)
SYSCALL_STUB(chimera_poll, 230)
SYSCALL_STUB(chimera_nanosleep, 240)
SYSCALL_STUB(chimera_posix_spawn, 244)
SYSCALL_STUB(chimera_yield, 248)
SYSCALL_STUB(chimera_spawn, 250)
SYSCALL_STUB(chimera_sysinfo, 251)
SYSCALL_STUB(chimera_proclist, 252)
SYSCALL_STUB(chimera_getcwd, 361)
SYSCALL_STUB(chimera_poll_event, 139)

// Mach IPC system traps
SYSCALL_STUB(chimera_mach_msg, 260)
SYSCALL_STUB(chimera_mach_port_allocate, 261)
SYSCALL_STUB(chimera_mach_register_service, 263)
SYSCALL_STUB(chimera_mach_lookup_service, 264)
SYSCALL_STUB(chimera_mach_port_deallocate, 265)
SYSCALL_STUB(chimera_mach_port_type, 266)
SYSCALL_STUB(chimera_task_self, 268)

.globl "_getgroups$DARWIN_EXTSN"
"_getgroups$DARWIN_EXTSN":
    jmp _chimera_getgroups

.globl "_select$DARWIN_EXTSN"
"_select$DARWIN_EXTSN":
    jmp _chimera_select

.globl __ioctl
__ioctl:
    jmp _chimera_ioctl

.globl __select
__select:
    jmp _chimera_select

.globl __sigprocmask
__sigprocmask:
    jmp _chimera_sigprocmask

.globl __pthread_once
__pthread_once:
    jmp _pthread_once




