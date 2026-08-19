/* =============================================================================
 * XIU Operating System — User-Space Syscall Stubs (Mach-O Darwin ABI)
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

SYSCALL_STUB(xiu_log, 253)

SYSCALL_STUB(xiu_exit, 1)

SYSCALL_STUB(xiu_fork, 2)
SYSCALL_STUB(xiu_read, 3)
SYSCALL_STUB(xiu_write, 4)
SYSCALL_STUB(xiu_open, 5)
SYSCALL_STUB(xiu_close, 6)
SYSCALL_STUB(xiu_wait4, 7)
SYSCALL_STUB(xiu_chdir, 12)
SYSCALL_STUB(xiu_ioctl, 16)
SYSCALL_STUB(xiu_stat, 188)
SYSCALL_STUB(xiu_lseek, 199)

SYSCALL_STUB(xiu_getpid, 20)
SYSCALL_STUB(xiu_execve, 59)
SYSCALL_STUB(xiu_getcwd, 79)
SYSCALL_STUB(xiu_fcntl, 92)
SYSCALL_STUB(xiu_mkdir, 83)
SYSCALL_STUB(xiu_rmdir, 84)
SYSCALL_STUB(xiu_unlink, 10)
SYSCALL_STUB(xiu_mmap, 197)
SYSCALL_STUB(xiu_munmap, 73)
SYSCALL_STUB(xiu_mach_msg, 200)
SYSCALL_STUB(xiu_mach_port_allocate, 201)
SYSCALL_STUB(xiu_mach_msg_pid, 202)
SYSCALL_STUB(xiu_mach_register_service, 203)
SYSCALL_STUB(xiu_mach_lookup_service, 204)
SYSCALL_STUB(xiu_mach_port_deallocate, 205)
SYSCALL_STUB(xiu_mach_port_type, 206)
SYSCALL_STUB(xiu_task_self, 208)

SYSCALL_STUB(xiu_getdents, 217)
SYSCALL_STUB(xiu_yield, 138)
SYSCALL_STUB(xiu_poll_event, 139)
SYSCALL_STUB(xiu_pipe, 42)
SYSCALL_STUB(xiu_dup2, 90)
SYSCALL_STUB(xiu_kill, 37)
SYSCALL_STUB(xiu_sigaction, 46)
SYSCALL_STUB(xiu_sigprocmask, 48)
SYSCALL_STUB(xiu_spawn, 250)
SYSCALL_STUB(xiu_proclist, 252)

// darwin BSD Socket Syscalls
SYSCALL_STUB(xiu_socket, 97)
SYSCALL_STUB(xiu_bind, 104)
SYSCALL_STUB(xiu_connect, 98)
SYSCALL_STUB(xiu_listen, 106)
SYSCALL_STUB(xiu_accept, 30)
SYSCALL_STUB(xiu_sendto, 133)
SYSCALL_STUB(xiu_recvfrom, 29)
SYSCALL_STUB(xiu_shutdown, 134)
SYSCALL_STUB(xiu_setsockopt, 105)
SYSCALL_STUB(xiu_getsockopt, 118)
SYSCALL_STUB(xiu_sysinfo, 251)
