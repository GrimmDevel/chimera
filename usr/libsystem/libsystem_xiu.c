#include "include/dirent.h"
#include "include/sys/resource.h"
#include "include/sys/stat.h"
#include "include/wchar.h"
#include "include/wctype.h"
#include "include/stdio.h"
#include "include/string.h"
#include "include/stdlib.h"
#include "include/unistd.h"
#include "include/signal.h"
#include <kernel/syscall.h>
#include <kernel/xiu_types.h>
#include "include/ctype.h"
#include "include/sys/times.h"
#include "include/sys/time.h"
#include "include/time.h"
#include "include/locale.h"
#include "include/sched.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#include "include/sys/select.h"
#include "include/sys/poll.h"
#include "include/sys/sysctl.h"
#include "include/sys/uio.h"
#include "include/sys/mman.h"
#include "include/sys/event.h"
#include "include/sys/shm.h"
#include "include/sys/ipc.h"
#include "include/fcntl.h"
#include "include/pwd.h"
#include "include/grp.h"
#include "include/sys/utsname.h"
#include "include/kvm.h"
#include "include/errno.h"

// prototypes for assembly stubs
extern i64 xiu_exit(u64 code);
extern i64 xiu_fork(void);
extern i64 xiu_wait4(int pid, int *status, int options, void *rusage);
extern i64 xiu_read(int fd, void *buf, usize len);
extern i64 xiu_write(int fd, const void *buf, usize len);
extern i64 xiu_open(const char *path, int flags, int mode);
extern i64 xiu_close(int fd);
extern i64 xiu_chdir(const char *path);
extern i64 xiu_getpid(void);
extern i64 xiu_execve(const char *path, char *const argv[], char *const envp[]);
extern i64 xiu_mmap(void *addr, usize len, int prot, int flags, int fd,
                    u64 offset);
extern i64 xiu_mach_msg(void *msg, u32 option, u32 send_sz, u32 rcv_sz,
                        u32 rcv_name, u32 timeout);
extern i64 xiu_getdents(int fd, void *buf, usize count);
extern i64 xiu_stat(const char *path, struct stat *buf);
extern i64 xiu_fstat(int fd, struct stat *buf);
extern i64 xiu_lstat(const char *path, struct stat *buf);
extern i64 xiu_mkdir(const char *path, u32 mode);
extern i64 xiu_rmdir(u64 path);
extern i64 xiu_unlink(u64 path);
extern i64 xiu_access(const char *path, int mode);
extern i64 xiu_chmod(const char *path, mode_t mode);
extern i64 xiu_fchmod(int fd, mode_t mode);
extern i64 xiu_chown(const char *path, uid_t owner, gid_t group);
extern i64 xiu_fchown(int fd, uid_t owner, gid_t group);
extern i64 xiu_truncate(const char *path, off_t length);
extern i64 xiu_ftruncate(int fd, off_t length);
extern i64 xiu_rename(const char *oldpath, const char *newpath);
extern i64 xiu_umask(int mask);
extern i64 xiu_ioctl(int fd, u64 cmd, void *arg);
extern i64 xiu_fcntl(int fd, int cmd, u64 arg);
extern i64 xiu_lseek(int fd, i64 offset, int whence);
extern i64 xiu_getcwd(char *buf, usize size);
extern i64 xiu_pipe(int pipefd[2]);
extern i64 xiu_dup(int oldfd);
extern i64 xiu_dup2(int oldfd, int newfd);
extern i64 xiu_readv(int fd, const struct iovec *iov, int iovcnt);
extern i64 xiu_writev(int fd, const struct iovec *iov, int iovcnt);
extern i64 xiu_pread(int fd, void *buf, usize count, off_t offset);
extern i64 xiu_pwrite(int fd, const void *buf, usize count, off_t offset);
extern i64 xiu_getuid(void);
extern i64 xiu_geteuid(void);
extern i64 xiu_setuid(uid_t uid);
extern i64 xiu_seteuid(uid_t euid);
extern i64 xiu_getgid(void);
extern i64 xiu_getegid(void);
extern i64 xiu_setgid(gid_t gid);
extern i64 xiu_setegid(gid_t egid);
extern i64 xiu_getgroups(int size, gid_t list[]);
extern i64 xiu_setgroups(int size, const gid_t *list);
extern i64 xiu_getlogin(char *name, usize namelen);
extern i64 xiu_setlogin(const char *name);
extern i64 xiu_getppid(void);
extern i64 xiu_getpgrp(void);
extern i64 xiu_setpgid(pid_t pid, pid_t pgid);
extern i64 xiu_setsid(void);
extern i64 xiu_poll(struct pollfd *fds, unsigned int nfds, int timeout);
extern i64 xiu_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
extern i64 xiu_sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
extern i64 xiu_gettimeofday(struct timeval *tv, void *tz);
extern i64 xiu_settimeofday(const struct timeval *tv, const void *tz);
extern i64 xiu_kill(int pid, int sig);
extern i64 xiu_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
extern i64 xiu_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
extern i64 xiu_spawn(const char *path, char *const argv[], char *const envp[],
                     const char *stdin_path, const char *stdout_path);
extern i64 xiu_mach_msg(void *msg, u32 opt, u32 ssz, u32 rsz, u32 rport,
                        u32 timeout);

static char *s_default_environ[] = {
    "PATH=/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin",
    "USER=root",
    "HOME=/Users/root",
    "SHELL=/bin/sh",
    "TERM=fbcon",
    NULL
};

char **environ = s_default_environ;
static int s_environ_allocated = 0;
static usize s_environ_capacity = 64;
static int s_xiu_errno = 0;

int *__error(void) {
    return &s_xiu_errno;
}
#undef errno
int errno = 0;

char ***_NSGetEnviron(void) {
    return &environ;
}

// high-level wrappers

static void (*g_atexit_funcs[32])(void);
static int g_atexit_count = 0;

int atexit(void (*func)(void)) {
    if (g_atexit_count < 32) {
        g_atexit_funcs[g_atexit_count++] = func;
        return 0;
    }
    return -1;
}

void exit(int code) {
  for (int i = g_atexit_count - 1; i >= 0; i--) {
    if (g_atexit_funcs[i]) {
      g_atexit_funcs[i]();
    }
  }
  xiu_exit((u64)code);
  for (;;)
    ;
}

ssize_t write(int fd, const void *buf, size_t len) {
  return xiu_write(fd, buf, len);
}

ssize_t read(int fd, void *buf, size_t len) {
  return xiu_read(fd, buf, len);
}

int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    i64 ret = xiu_open(path, flags, mode);
    if (ret < 0) {
        errno = (int)-ret;
        return -1;
    }
    return (int)ret;
}

int close(int fd) { return (int)xiu_close(fd); }

int stat(const char *path, struct stat *buf) {
  return (int)xiu_stat(path, buf);
}

int mkdir(const char *path, mode_t mode) { return (int)xiu_mkdir(path, mode); }

int ioctl(int fd, u64 cmd, void *arg) { return (int)xiu_ioctl(fd, cmd, arg); }

char *getcwd(char *buf, usize size) {
  if (!buf && size == 0) {
    size = 1024;
    buf = malloc(size);
    if (!buf)
      return NULL;
  }
  if (xiu_getcwd(buf, size) < 0)
    return NULL;
  return buf;
}


void *mmap(void *addr, usize length, int prot, int flags, int fd, off_t offset) {
  i64 res = xiu_mmap(addr, length, prot, flags, fd, (u64)offset);
  if (res < 0)
    return (void *)-1;
  return (void *)res;
}

extern i64 xiu_munmap(void *addr, usize len);

int munmap(void *addr, usize length) {
  return (int)xiu_munmap(addr, length);
}
int memfd_create(const char *name, unsigned int flags) { return -1; }

// directory enumeration

struct __xiu_dir_stream {
  DIR dir;
  struct dirent entry;
};

DIR *opendir(const char *name) {
  int fd = open(name, 0, 0);
  if (fd < 0)
    return NULL;

  struct __xiu_dir_stream *xdir = (struct __xiu_dir_stream *)malloc(sizeof(struct __xiu_dir_stream));
  if (!xdir) {
    close(fd);
    return NULL;
  }
  memset(xdir, 0, sizeof(*xdir));
  xdir->dir.__dd_fd = fd;
  return (DIR *)xdir;
}

struct dirent *readdir(DIR *dirp) {
  if (!dirp) return NULL;
  struct __xiu_dir_stream *xdir = (struct __xiu_dir_stream *)dirp;
  if (xiu_getdents(xdir->dir.__dd_fd, &xdir->entry, sizeof(struct dirent)) <= 0) {
    return NULL;
  }
  return &xdir->entry;
}

int closedir(DIR *dirp) {
  if (!dirp) return -1;
  struct __xiu_dir_stream *xdir = (struct __xiu_dir_stream *)dirp;
  int fd = xdir->dir.__dd_fd;
  free(xdir);
  return close(fd);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §1  Mach IPC Library Wrappers
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include "include/mach/mach.h"
#include "include/servers/bootstrap.h"

typedef mach_msg_header_t xiu_msg_header_t;

extern i64 xiu_mach_port_allocate(ipc_space_t space, mach_port_right_t right,
                                  mach_port_name_t *name);
extern i64 xiu_mach_port_deallocate(u64 space, u64 name);
extern i64 xiu_mach_port_type(u64 space, u64 name, u64 ptype_out);
extern i64 xiu_mach_lookup_service(const char *name,
                                   mach_port_name_t *port_out);
extern i64 xiu_mach_msg(void *msg, u32 option, u32 send_sz, u32 rcv_sz,
                        u32 rcv_name, u32 timeout);
extern void xiu_yield(void);
extern i64 xiu_log(const char *msg);

// allocate a new receive-right Mach port for this task
i64 xiu_mach_alloc_port(u32 *port_out) {
  return xiu_mach_port_allocate(MACH_PORT_NULL, MACH_PORT_RIGHT_RECEIVE,
                                port_out);
}

// synchronous send+receive
i64 xiu_mach_send_recv(u32 dst_port, void *msg, u32 send_sz, void *reply_buf,
                       u32 rcv_sz, u32 reply_port) {
  xiu_msg_header_t *hdr = (xiu_msg_header_t *)msg;
  hdr->msgh_remote_port = dst_port;
  hdr->msgh_local_port = reply_port;
  hdr->msgh_size = send_sz;
  hdr->msgh_bits = 0x0013;

  // 1. send message
  i64 rc = xiu_mach_msg(msg, 1, send_sz, 0, 0, 5000);
  if (rc < 0)
    return rc;

  // 2. wait for reply
  u8 rcv_temp[2048];
  do {
    rc = xiu_mach_msg(rcv_temp, 2, 0, sizeof(rcv_temp), reply_port, 5000);
    if (rc < 0)
      xiu_yield();
  } while (rc < 0);
  
  // copy the actual message back to the caller's buffer
  xiu_msg_header_t *rep = (xiu_msg_header_t *)rcv_temp;
  u32 actual_sz = rep->msgh_size;
  if (actual_sz > rcv_sz) actual_sz = rcv_sz;
  memcpy(reply_buf, rcv_temp, actual_sz);
  return rc;
}

// fire-and-forget send
i64 XIUPortSendMessage(u32 port, void *msg, u32 size) {
  xiu_msg_header_t *hdr = (xiu_msg_header_t *)msg;
  hdr->msgh_remote_port = port;
  hdr->msgh_size = size;
  hdr->msgh_bits = 0x00001200;
  return xiu_mach_msg(msg, 1, size, 0, 0, 0);
}

pid_t fork(void) { return (pid_t)xiu_fork(); }

pid_t waitpid(pid_t pid, int *status, int options) {
    i64 ret = xiu_wait4(pid, status, options, NULL);
    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    return (pid_t)ret;
}

pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage) {
    i64 ret = xiu_wait4(pid, status, options, rusage);
    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    return (pid_t)ret;
}

pid_t getpid(void) { return (pid_t)xiu_getpid(); }
int pipe(int pipefd[2]) { return (int)xiu_pipe(pipefd); }

i64 lseek(int fd, i64 offset, int whence) { return xiu_lseek(fd, offset, whence); }
int fcntl(int fd, int cmd, ...) {
  __builtin_va_list ap;
  u64 arg = 0;

  __builtin_va_start(ap, cmd);
  if (cmd == 0 || cmd == 2 || cmd == 4) {
    arg = (u64)__builtin_va_arg(ap, int);
  }
  __builtin_va_end(ap);

  return (int)xiu_fcntl(fd, cmd, arg);
}

extern i64 xiu_unlink(u64 path);
extern i64 xiu_rmdir(u64 path);

int unlink(const char *path) {
  if (!path) return -1;
  i64 ret = xiu_unlink((u64)path);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int rmdir(const char *path) {
  if (!path) return -1;
  i64 ret = xiu_rmdir((u64)path);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int mkstemp(char *tmpl) { return -1; }

int access(const char *path, int mode) {
  if (!path) { errno = EFAULT; return -1; }
  i64 ret = xiu_access(path, mode);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

mode_t umask(mode_t mask) {
  return (mode_t)xiu_umask(mask);
}

int chmod(const char *path, mode_t mode) {
  i64 ret = xiu_chmod(path, mode);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int fchmod(int fd, mode_t mode) {
  i64 ret = xiu_fchmod(fd, mode);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int chown(const char *path, uid_t owner, gid_t group) {
  i64 ret = xiu_chown(path, owner, group);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int fchown(int fd, uid_t owner, gid_t group) {
  i64 ret = xiu_fchown(fd, owner, group);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int truncate(const char *path, off_t length) {
  i64 ret = xiu_truncate(path, length);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int ftruncate(int fd, off_t length) {
  i64 ret = xiu_ftruncate(fd, length);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int rename(const char *oldpath, const char *newpath) {
  i64 ret = xiu_rename(oldpath, newpath);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int dup(int oldfd) {
  i64 ret = xiu_dup(oldfd);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return (int)ret;
}

int dup2(int oldfd, int newfd) {
  i64 ret = xiu_dup2(oldfd, newfd);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return (int)ret;
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
  i64 ret = xiu_readv(fd, iov, iovcnt);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return (ssize_t)ret;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
  i64 ret = xiu_writev(fd, iov, iovcnt);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return (ssize_t)ret;
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
  i64 ret = xiu_pread(fd, buf, count, offset);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return (ssize_t)ret;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
  i64 ret = xiu_pwrite(fd, buf, count, offset);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return (ssize_t)ret;
}

uid_t getuid(void) { return (uid_t)xiu_getuid(); }
uid_t geteuid(void) { return (uid_t)xiu_geteuid(); }
int setuid(uid_t uid) {
  i64 ret = xiu_setuid(uid);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}
int seteuid(uid_t euid) {
  i64 ret = xiu_seteuid(euid);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}
gid_t getgid(void) { return (gid_t)xiu_getgid(); }
gid_t getegid(void) { return (gid_t)xiu_getegid(); }
int setgid(gid_t gid) {
  i64 ret = xiu_setgid(gid);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}
int setegid(gid_t egid) {
  i64 ret = xiu_setegid(egid);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}
int getgroups(int size, gid_t list[]) {
  i64 ret = xiu_getgroups(size, list);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return (int)ret;
}
int setgroups(int size, const gid_t *list) {
  i64 ret = xiu_setgroups(size, list);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}
pid_t getppid(void) { return (pid_t)xiu_getppid(); }
pid_t getpgrp(void) { return (pid_t)xiu_getpgrp(); }
int setpgid(pid_t pid, pid_t pgid) {
  i64 ret = xiu_setpgid(pid, pgid);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}
pid_t setsid(void) {
  i64 ret = xiu_setsid();
  if (ret < 0) { errno = (int)-ret; return -1; }
  return (pid_t)ret;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
  i64 ret = xiu_poll(fds, nfds, timeout);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return (int)ret;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  i64 ret = xiu_select(nfds, readfds, writefds, exceptfds, timeout);
  return (int)ret;
}

static unsigned long _next_rand = 1;

void srandom(unsigned int seed) {
  _next_rand = seed;
}

void srandomdev(void) {
  srandom((unsigned int)time(NULL));
}

long random(void) {
  _next_rand = _next_rand * 1103515245 + 12345;
  return (long)((unsigned long)(_next_rand / 65536) % 0x80000000);
}

char *initstate(unsigned seed, char *state, size_t n) {
  (void)n;
  srandom(seed);
  return state;
}

char *setstate(const char *state) {
  return (char *)state;
}

int gettimeofday(struct timeval *tv, void *tz) {
  i64 ret = xiu_gettimeofday(tv, tz);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int settimeofday(const struct timeval *tv, const struct timezone *tz) {
  i64 ret = xiu_settimeofday(tv, (const void *)tz);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
  i64 ret = xiu_sysctl(name, namelen, oldp, oldlenp, newp, newlen);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
  if (!name) { errno = EFAULT; return -1; }
  int mib[2];
  if (strcmp(name, "kern.ostype") == 0) {
    mib[0] = CTL_KERN; mib[1] = KERN_OSTYPE;
  } else if (strcmp(name, "kern.osrelease") == 0) {
    mib[0] = CTL_KERN; mib[1] = KERN_OSRELEASE;
  } else if (strcmp(name, "kern.version") == 0) {
    mib[0] = CTL_KERN; mib[1] = KERN_VERSION;
  } else if (strcmp(name, "kern.hostname") == 0) {
    mib[0] = CTL_KERN; mib[1] = KERN_HOSTNAME;
  } else if (strcmp(name, "kern.maxvnodes") == 0) {
    mib[0] = CTL_KERN; mib[1] = KERN_MAXVNODES;
  } else if (strcmp(name, "kern.maxproc") == 0) {
    mib[0] = CTL_KERN; mib[1] = KERN_MAXPROC;
  } else if (strcmp(name, "hw.machine") == 0) {
    mib[0] = CTL_HW; mib[1] = HW_MACHINE;
  } else if (strcmp(name, "hw.model") == 0) {
    mib[0] = CTL_HW; mib[1] = HW_MODEL;
  } else if (strcmp(name, "hw.ncpu") == 0 || strcmp(name, "hw.activecpu") == 0) {
    mib[0] = CTL_HW; mib[1] = HW_NCPU;
  } else if (strcmp(name, "hw.pagesize") == 0) {
    mib[0] = CTL_HW; mib[1] = HW_PAGESIZE;
  } else if (strcmp(name, "hw.memsize") == 0 || strcmp(name, "hw.physmem") == 0) {
    mib[0] = CTL_HW; mib[1] = HW_MEMSIZE;
  } else if (strcmp(name, "hw.byteorder") == 0) {
    mib[0] = CTL_HW; mib[1] = HW_BYTEORDER;
  } else {
    errno = ENOENT;
    return -1;
  }
  return sysctl(mib, 2, oldp, oldlenp, newp, newlen);
}

// Darwin Rune Locale & Character Tables
#include <runetype.h>

_RuneLocale _DefaultRuneLocale = {
    _RUNE_MAGIC_A,
    "NONE",
    NULL,
    NULL,
    0xFFFD,
    {
        /* 00-1F */
        _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C,
        _CTYPE_C, _CTYPE_C|_CTYPE_S|_CTYPE_B, _CTYPE_C|_CTYPE_S, _CTYPE_C|_CTYPE_S,
        _CTYPE_C|_CTYPE_S, _CTYPE_C|_CTYPE_S, _CTYPE_C, _CTYPE_C,
        _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C,
        _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C, _CTYPE_C,
        /* 20-2F */
        _CTYPE_S|_CTYPE_B|_CTYPE_R, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G,
        _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G,
        _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G,
        _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G,
        /* 30-39 (0-9) */
        _CTYPE_D|_CTYPE_R|_CTYPE_G|_CTYPE_X|0, _CTYPE_D|_CTYPE_R|_CTYPE_G|_CTYPE_X|1,
        _CTYPE_D|_CTYPE_R|_CTYPE_G|_CTYPE_X|2, _CTYPE_D|_CTYPE_R|_CTYPE_G|_CTYPE_X|3,
        _CTYPE_D|_CTYPE_R|_CTYPE_G|_CTYPE_X|4, _CTYPE_D|_CTYPE_R|_CTYPE_G|_CTYPE_X|5,
        _CTYPE_D|_CTYPE_R|_CTYPE_G|_CTYPE_X|6, _CTYPE_D|_CTYPE_R|_CTYPE_G|_CTYPE_X|7,
        _CTYPE_D|_CTYPE_R|_CTYPE_G|_CTYPE_X|8, _CTYPE_D|_CTYPE_R|_CTYPE_G|_CTYPE_X|9,
        /* 3A-40 */
        _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G,
        _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G,
        /* 41-5A (A-Z) */
        _CTYPE_U|_CTYPE_X|_CTYPE_R|_CTYPE_G|_CTYPE_A|10, _CTYPE_U|_CTYPE_X|_CTYPE_R|_CTYPE_G|_CTYPE_A|11,
        _CTYPE_U|_CTYPE_X|_CTYPE_R|_CTYPE_G|_CTYPE_A|12, _CTYPE_U|_CTYPE_X|_CTYPE_R|_CTYPE_G|_CTYPE_A|13,
        _CTYPE_U|_CTYPE_X|_CTYPE_R|_CTYPE_G|_CTYPE_A|14, _CTYPE_U|_CTYPE_X|_CTYPE_R|_CTYPE_G|_CTYPE_A|15,
        _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_U|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        /* 5B-60 */
        _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G,
        _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G,
        /* 61-7A (a-z) */
        _CTYPE_L|_CTYPE_X|_CTYPE_R|_CTYPE_G|_CTYPE_A|10, _CTYPE_L|_CTYPE_X|_CTYPE_R|_CTYPE_G|_CTYPE_A|11,
        _CTYPE_L|_CTYPE_X|_CTYPE_R|_CTYPE_G|_CTYPE_A|12, _CTYPE_L|_CTYPE_X|_CTYPE_R|_CTYPE_G|_CTYPE_A|13,
        _CTYPE_L|_CTYPE_X|_CTYPE_R|_CTYPE_G|_CTYPE_A|14, _CTYPE_L|_CTYPE_X|_CTYPE_R|_CTYPE_G|_CTYPE_A|15,
        _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A, _CTYPE_L|_CTYPE_R|_CTYPE_G|_CTYPE_A,
        /* 7B-7F */
        _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_P|_CTYPE_R|_CTYPE_G,
        _CTYPE_P|_CTYPE_R|_CTYPE_G, _CTYPE_C,
    },
};
_RuneLocale *_CurrentRuneLocale = &_DefaultRuneLocale;

int __maskrune(ct_rune_t c, unsigned long f) {
    if (c < 0 || c == -1) return 0;
    return (_DefaultRuneLocale.__runetype[c & 0xff] & f) != 0;
}

unsigned long __runetype(ct_rune_t c) {
    if (c < 0 || c == -1) return 0;
    return _DefaultRuneLocale.__runetype[c & 0xff];
}

int __tolower(ct_rune_t c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

int __toupper(ct_rune_t c) {
    if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
    return c;
}

#include "include/termios.h"

#undef sigemptyset
#undef sigfillset
#undef sigaddset
#undef sigdelset
#undef sigismember
int sigemptyset(sigset_t *set) { if (set) *set = 0; return 0; }
int sigfillset(sigset_t *set) { if (set) *set = ~(sigset_t)0; return 0; }
int sigaddset(sigset_t *set, int signum) { if (set) *set |= (1U << (signum - 1)); return 0; }
int sigdelset(sigset_t *set, int signum) { if (set) *set &= ~(1U << (signum - 1)); return 0; }
int sigismember(const sigset_t *set, int signum) { if (!set) return 0; return (*set & (1U << (signum - 1))) != 0; }


// string Utilities

#undef strlen
#undef strcpy
#undef strncpy
#undef strcmp
#undef strncmp
#undef strcat
#undef strncat
#undef strchr
#undef strrchr
#undef strstr
#undef strdup
#undef memcpy
#undef memset
#undef memmove
#undef memcmp
#undef bzero
#undef __bzero
#undef sprintf
#undef snprintf
#undef vsnprintf

usize strlen(const char *s) {
  usize len = 0;
  while (s[len])
    len++;
  return len;
}

char *strcpy(char *dest, const char *src) {
  char *d = dest;
  while ((*d++ = *src++))
    ;
  return dest;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, usize n) {
  while (n && *s1 && (*s1 == *s2)) {
    s1++;
    s2++;
    n--;
  }
  if (n == 0)
    return 0;
  return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

void *memcpy(void *dest, const void *src, usize n) {
  void *orig = dest;
  __asm__ volatile("cld; rep movsb"
                   : "+D"(dest), "+S"(src), "+c"(n)
                   :
                   : "memory");
  return orig;
}

void *memset(void *s, int c, usize n) {
  void *orig = s;
  __asm__ volatile("cld; rep stosb"
                   : "+D"(s), "+c"(n)
                   : "a"((unsigned char)c)
                   : "memory");
  return orig;
}

void bzero(void *s, usize n) {
  __asm__ volatile("cld; rep stosb"
                   : "+D"(s), "+c"(n)
                   : "a"((unsigned char)0)
                   : "memory");
}

void __bzero(void *s, usize n) {
  __asm__ volatile("cld; rep stosb"
                   : "+D"(s), "+c"(n)
                   : "a"((unsigned char)0)
                   : "memory");
}

void memset_pattern16(void *b, const void *pattern16, usize len) {
  unsigned char *dst = (unsigned char *)b;
  const unsigned char *pat = (const unsigned char *)pattern16;
  while (len >= 16) {
    __builtin_memcpy(dst, pat, 16);
    dst += 16;
    len -= 16;
  }
  if (len > 0) {
    __builtin_memcpy(dst, pat, len);
  }
}

double ldexp(double x, int exp) {
  double factor = 1.0;
  if (exp > 0) {
    while (exp--) factor *= 2.0;
  } else if (exp < 0) {
    while (exp++) factor *= 0.5;
  }
  return x * factor;
}

long double ldexpl(long double x, int exp) {
  long double factor = 1.0L;
  if (exp > 0) {
    while (exp--) factor *= 2.0L;
  } else if (exp < 0) {
    while (exp++) factor *= 0.5L;
  }
  return x * factor;
}

static void format_putc(char *buf, usize n, usize *pos, char c) {
  if (buf && n > 0 && *pos + 1 < n)
    buf[*pos] = c;
  (*pos)++;
}

static void format_puts(char *buf, usize n, usize *pos, const char *s) {
  if (!s)
    s = "(null)";
  while (*s)
    format_putc(buf, n, pos, *s++);
}

static void format_unsigned(char *buf, usize n, usize *pos, u64 val, int radix) {
  static const char digits[] = "0123456789abcdef";
  char tmp[65];
  int i = 0;

  if (val == 0) {
    tmp[i++] = '0';
  } else {
    while (val > 0) {
      tmp[i++] = digits[val % (u64)radix];
      val /= (u64)radix;
    }
  }

  while (i > 0)
    format_putc(buf, n, pos, tmp[--i]);
}

static void format_signed(char *buf, usize n, usize *pos, i64 val) {
  u64 mag;
  if (val < 0) {
    format_putc(buf, n, pos, '-');
    mag = (u64)(-(val + 1)) + 1;
  } else {
    mag = (u64)val;
  }
  format_unsigned(buf, n, pos, mag, 10);
}

int vsnprintf(char *buf, usize n, const char *fmt, __builtin_va_list args) {
  usize pos = 0;

  while (*fmt) {
    if (*fmt != '%') {
      format_putc(buf, n, &pos, *fmt++);
      continue;
    }

    fmt++; // skip '%'
    if (*fmt == '%') {
      format_putc(buf, n, &pos, '%');
      fmt++;
      continue;
    }

    // 1. Flags
    bool left_align = false;
    bool zero_pad = false;
    while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#' || *fmt == '0') {
      if (*fmt == '-') left_align = true;
      else if (*fmt == '0') zero_pad = true;
      fmt++;
    }

    // 2. Width
    int width = 0;
    if (*fmt == '*') {
      width = __builtin_va_arg(args, int);
      if (width < 0) { left_align = true; width = -width; }
      fmt++;
    } else {
      while (*fmt >= '0' && *fmt <= '9') {
        width = width * 10 + (*fmt - '0');
        fmt++;
      }
    }

    // 3. Precision
    int precision = -1;
    if (*fmt == '.') {
      fmt++;
      if (*fmt == '*') {
        precision = __builtin_va_arg(args, int);
        fmt++;
      } else {
        precision = 0;
        while (*fmt >= '0' && *fmt <= '9') {
          precision = precision * 10 + (*fmt - '0');
          fmt++;
        }
      }
    }

    // 4. Length modifiers
    int long_count = 0;
    while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 'j' || *fmt == 't') {
      if (*fmt == 'l') long_count++;
      else if (*fmt == 'z') long_count = 1;
      fmt++;
    }

    // 5. Specifier
    char spec = *fmt;
    if (!spec) break;
    fmt++;

    if (spec == 's') {
      char *s = __builtin_va_arg(args, char *);
      if (!s || (uptr)s < 0x1000 || (uptr)s >= 0x0000800000000000ULL) s = "(null)";
      usize slen = 0;
      while (s[slen] && (precision < 0 || (int)slen < precision)) slen++;

      if (!left_align && width > (int)slen) {
        char pad = zero_pad ? '0' : ' ';
        for (int p = 0; p < width - (int)slen; p++) format_putc(buf, n, &pos, pad);
      }
      for (usize p = 0; p < slen; p++) format_putc(buf, n, &pos, s[p]);
      if (left_align && width > (int)slen) {
        for (int p = 0; p < width - (int)slen; p++) format_putc(buf, n, &pos, ' ');
      }
    } else if (spec == 'm') {
      const char *s = strerror(errno);
      if (!s) s = "Unknown error";
      usize slen = strlen(s);
      for (usize p = 0; p < slen; p++) format_putc(buf, n, &pos, s[p]);
    } else if (spec == 'c') {
      char c = (char)__builtin_va_arg(args, int);
      format_putc(buf, n, &pos, c);
    } else if (spec == 'd' || spec == 'i') {
      i64 val;
      if (long_count > 1) val = __builtin_va_arg(args, long long);
      else if (long_count == 1) val = __builtin_va_arg(args, long);
      else val = __builtin_va_arg(args, int);
      format_signed(buf, n, &pos, val);
    } else if (spec == 'u') {
      u64 val;
      if (long_count > 1) val = __builtin_va_arg(args, unsigned long long);
      else if (long_count == 1) val = __builtin_va_arg(args, unsigned long);
      else val = __builtin_va_arg(args, unsigned int);
      format_unsigned(buf, n, &pos, val, 10);
    } else if (spec == 'o') {
      u64 val;
      if (long_count > 1) val = __builtin_va_arg(args, unsigned long long);
      else if (long_count == 1) val = __builtin_va_arg(args, unsigned long);
      else val = __builtin_va_arg(args, unsigned int);
      format_unsigned(buf, n, &pos, val, 8);
    } else if (spec == 'x' || spec == 'X') {
      u64 val;
      if (long_count > 1) val = __builtin_va_arg(args, unsigned long long);
      else if (long_count == 1) val = __builtin_va_arg(args, unsigned long);
      else val = __builtin_va_arg(args, unsigned int);
      format_unsigned(buf, n, &pos, val, 16);
    } else if (spec == 'p') {
      u64 val = __builtin_va_arg(args, u64);
      format_puts(buf, n, &pos, "0x");
      format_unsigned(buf, n, &pos, val, 16);
    } else {
      format_putc(buf, n, &pos, spec);
    }
  }

  if (buf && n > 0) {
    usize nul = (pos < n) ? pos : n - 1;
    buf[nul] = '\0';
  }
  return (int)pos;
}

int sprintf(char *buf, const char *fmt, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, fmt);
  int res = vsnprintf(buf, 0xFFFFFFFF, fmt, args);
  __builtin_va_end(args);
  return res;
}

int snprintf(char *buf, usize n, const char *fmt, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, fmt);
  int res = vsnprintf(buf, n, fmt, args);
  __builtin_va_end(args);
  return res;
}

int printf(const char *fmt, ...) {
  char buf[1024];
  __builtin_va_list args;
  __builtin_va_start(args, fmt);
  int res = vsnprintf(buf, sizeof(buf), fmt, args);
  __builtin_va_end(args);
  write(1, buf, res);
  return res;
}

int puts(const char *s) {
  int len = strlen(s);
  write(1, s, len);
  write(1, "\n", 1);
  return len + 1;
}

int putchar(int c) {
  unsigned char ch = (unsigned char)c;
  write(1, &ch, 1);
  return c;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
  return (int)xiu_execve(path, argv, envp);
}

// memory Management

struct malloc_chunk {
  usize size;
  struct malloc_chunk *next;
  int free;
};

static struct malloc_chunk *g_malloc_list = NULL;

static void split_chunk(struct malloc_chunk *chunk, usize size) {
  if (chunk->size >= size + sizeof(struct malloc_chunk) + 16) {
    struct malloc_chunk *new_chunk = (struct malloc_chunk *)((u8 *)(chunk + 1) + size);
    new_chunk->size = chunk->size - size - sizeof(struct malloc_chunk);
    new_chunk->free = 1;
    new_chunk->next = chunk->next;
    chunk->size = size;
    chunk->next = new_chunk;
  }
}

void *malloc(usize size) {
  if (size == 0) size = 16;
  if (size > 0x40000000ULL) return NULL;
  
  // align to 16 bytes
  size = (size + 15) & ~15;

  struct malloc_chunk *curr = g_malloc_list;
  while (curr) {
    if (curr->free && curr->size >= size) {
      split_chunk(curr, size);
      curr->free = 0;
      return (void *)(curr + 1);
    }
    curr = curr->next;
  }

  // allocate new pages (minimum 64KB)
  usize alloc_size = size + sizeof(struct malloc_chunk);
  if (alloc_size < 65536) alloc_size = 65536;
  alloc_size = (alloc_size + 4095) & ~4095;
  
  void *ptr = mmap(NULL, alloc_size, 3, 0x22, -1, 0);
  if (ptr == (void *)-1) return NULL;

  struct malloc_chunk *chunk = (struct malloc_chunk *)ptr;
  chunk->size = alloc_size - sizeof(struct malloc_chunk);
  chunk->free = 0;
  chunk->next = g_malloc_list;
  g_malloc_list = chunk;

  split_chunk(chunk, size);
  return (void *)(chunk + 1);
}

void *realloc(void *ptr, usize size) {
  if (!ptr) return malloc(size);
  if (size == 0) {
    free(ptr);
    return NULL;
  }
  
  struct malloc_chunk *chunk = (struct malloc_chunk *)ptr - 1;
  if (chunk->size >= size) return ptr;

  void *new_ptr = malloc(size);
  if (!new_ptr) return NULL;
  
  memcpy(new_ptr, ptr, chunk->size);
  free(ptr);
  return new_ptr;
}

void *calloc(usize nmemb, usize size) {
  usize total = nmemb * size;
  void *ptr = malloc(total);
  if (ptr) __builtin_memset(ptr, 0, total);
  return ptr;
}

void *memmove(void *dest, const void *src, usize n) {
  void *orig = dest;
  if (dest == src || n == 0) return dest;
  if (dest < src) {
    __asm__ volatile("cld; rep movsb" : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
  } else {
    u8 *d = (u8 *)dest + n - 1;
    const u8 *s = (const u8 *)src + n - 1;
    __asm__ volatile("std; rep movsb; cld" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
  }
  return orig;
}

void free(void *ptr) {
  if (!ptr) return;
  struct malloc_chunk *chunk = (struct malloc_chunk *)ptr - 1;
  chunk->free = 1;

  // coalesce adjacent free chunks
  struct malloc_chunk *curr = g_malloc_list;
  while (curr) {
    while (curr->free && curr->next && curr->next->free &&
           (u8 *)(curr + 1) + curr->size == (u8 *)curr->next) {
      curr->size += sizeof(struct malloc_chunk) + curr->next->size;
      curr->next = curr->next->next;
    }
    curr = curr->next;
  }
}


int fstat(int fd, struct stat *buf) {
  i64 ret = xiu_fstat(fd, buf);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}

int lstat(const char *path, struct stat *buf) {
  i64 ret = xiu_lstat(path, buf);
  if (ret < 0) { errno = (int)-ret; return -1; }
  return 0;
}


int sched_yield(void) {
    xiu_yield();
    return 0;
}

#include <sys/time.h>

extern i64 xiu_nanosleep(const void *req, void *rem);

int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req) return -1;
    return (int)xiu_nanosleep(req, rem);
}

int usleep(unsigned int usec) {
    struct timespec ts;
    ts.tv_sec = usec / 1000000;
    ts.tv_nsec = (long)(usec % 1000000) * 1000;
    return nanosleep(&ts, NULL);
}

unsigned int sleep(unsigned int seconds) {
    struct timespec ts;
    ts.tv_sec = (long)seconds;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);
    return 0;
}

int abs(int j) {
    return j < 0 ? -j : j;
}

long long atoll(const char *str) {
    if (!str) return 0;
    while (*str == ' ' || *str == '\t' || *str == '\n') str++;
    int sign = 1;
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') { str++; }
    long long val = 0;
    while (*str >= '0' && *str <= '9') {
        val = val * 10 + (*str - '0');
        str++;
    }
    return sign * val;
}

int memcmp(const void *s1, const void *s2, usize n) {
    const unsigned char *p1 = s1, *p2 = s2;
    for (usize i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
}



void perror(const char *s) {
    if (s && *s) printf("%s: error\n", s);
    else printf("error\n");
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n) return -1;
    int fd = stream ? (((uptr)stream <= 2) ? (int)(uptr)stream : stream->_file) : 0;
    if (!*lineptr) {
        *n = 128;
        *lineptr = malloc(*n);
    }
    usize pos = 0;
    while (1) {
        char c;
        if (read(fd, &c, 1) <= 0) break;
        if (pos + 2 >= *n) {
            *n *= 2;
            *lineptr = realloc(*lineptr, *n);
        }
        (*lineptr)[pos++] = c;
        if (c == '\n') break;
    }
    if (pos == 0) return -1;
    (*lineptr)[pos] = '\0';
    return (i64)pos;
}

time_t time(time_t *tloc) {
    time_t now = 1700000000;
    if (tloc) *tloc = now;
    return now;
}

// string functions
char *strncpy(char *dest, const char *src, usize n) {
  usize i;
  for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
  for (; i < n; i++) dest[i] = '\0';
  return dest;
}

char *strchr(const char *s, int c) {
  while (*s) { if (*s == (char)c) return (char *)s; s++; }
  if (c == 0) return (char *)s;
  return NULL;
}

char *strrchr(const char *s, int c) {
  char *last = NULL;
  while (*s) { if (*s == (char)c) last = (char *)s; s++; }
  if (c == 0) return (char *)s;
  return last;
}

char *strstr(const char *haystack, const char *needle) {
  if (!*needle) return (char *)haystack;
  usize nlen = strlen(needle);
  while (*haystack) {
    if (*haystack == *needle && strncmp(haystack, needle, nlen) == 0)
      return (char *)haystack;
    haystack++;
  }
  return NULL;
}

usize strspn(const char *s, const char *accept) {
  usize n = 0;
  while (s[n] && strchr(accept, s[n])) n++;
  return n;
}

char *strpbrk(const char *s, const char *accept) {
  while (*s) { if (strchr(accept, *s)) return (char *)s; s++; }
  return NULL;
}

char *strdup(const char *s) {
  usize len = strlen(s) + 1;
  char *p = (char *)malloc(len);
  if (p) memcpy(p, s, len);
  return p;
}

int atoi(const char *s) {
  int res = 0, sign = 1;
  while (*s == ' ' || *s == '\t') s++;
  if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
  while (*s >= '0' && *s <= '9') res = res * 10 + (*s++ - '0');
  return res * sign;
}

int stat64(const char *path, struct stat64 *buf) {
    // struct stat64 is compatible with struct stat in our minimal libc for now
    return stat(path, (struct stat *)buf);
}

int lstat64(const char *path, struct stat64 *buf) {
    return stat64(path, buf);
}

void abort(void) {
    kill(getpid(), 6);
    exit(134);
}

pid_t vfork(void) {
    return fork();
}

int sigsuspend(const sigset_t *mask) {
    (void)mask;
    return -1;
}

void __assert_fail(const char *expr, const char *file, int line, const char *func) {
    printf("Assertion failed: %s (%s:%d: %s)\n", expr, file, line, func ? func : "?");
    abort();
}

int fprintf(FILE *f, const char *fmt, ...) {
  char buf[2048];
  __builtin_va_list args;
  __builtin_va_start(args, fmt);
  int res = vsnprintf(buf, sizeof(buf), fmt, args);
  __builtin_va_end(args);
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : (f ? f->_file : 1);
  write(fd, buf, res);
  return res;
}

int vfprintf(FILE *f, const char *fmt, __builtin_va_list args) {
  char buf[2048];
  int res = vsnprintf(buf, sizeof(buf), fmt, args);
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : (f ? f->_file : 1);
  write(fd, buf, res);
  return res;
}

int vprintf(const char *fmt, __builtin_va_list args) {
  return vfprintf(stdout, fmt, args);
}

FILE *fopen(const char *path, const char *mode) {
  int flags = 0; // o_rdonly
  if (mode && (strchr(mode, 'w') || strchr(mode, 'a'))) flags = 0x0001 | 0x0200 | 0x0400;
  else if (mode && strchr(mode, '+')) flags = 0x0002;
  
  int fd = open(path, flags, 0644);
  if (fd < 0) {
    errno = 2; // enoent
    return NULL;
  }
  FILE *f = (FILE *)malloc(sizeof(FILE));
  if (!f) {
    close(fd);
    errno = 12; // enomem
    return NULL;
  }
  memset(f, 0, sizeof(*f));
  f->_file = fd;
  return f;
}

FILE *freopen(const char *path, const char *mode, FILE *f) {
  if (f && (uptr)f > 2) fclose(f);
  return fopen(path, mode);
}

FILE *fdopen(int fd, const char *mode) {
  (void)mode;
  FILE *f = (FILE *)malloc(sizeof(FILE));
  if (!f) return NULL;
  memset(f, 0, sizeof(*f));
  f->_file = fd;
  return f;
}
int fclose(FILE *f) {
  if (!f) return -1;
  if ((uptr)f > 2) {
    close(f->_file);
    free(f);
  }
  return 0;
}

static FILE __sF_stdin  = { ._file = 0, ._flags = 0x0001 };
static FILE __sF_stdout = { ._file = 1, ._flags = 0x0002 };
static FILE __sF_stderr = { ._file = 2, ._flags = 0x0002 };

FILE *__stdinp  = &__sF_stdin;
FILE *__stdoutp = &__sF_stdout;
FILE *__stderrp = &__sF_stderr;

FILE *fopen$DARWIN_EXTSN(const char *path, const char *mode) {
  return fopen(path, mode);
}

FILE *fdopen$DARWIN_EXTSN(int fd, const char *mode) {
  return fdopen(fd, mode);
}

long sysconf(int name) {
  switch (name) {
  case 1: return 262144;
  case 2: return 64;
  case 3: return 100;
  case 4: return 16;
  case 5: return 256;
  case 29: return 4096;
  case 58: return 1;
  default: return -1;
  }
}

void *dlopen(const char *filename, int flag) { (void)filename; (void)flag; return (void *)0; }
int dlclose(void *handle) { (void)handle; return 0; }
void *dlsym(void *handle, const char *symbol) { (void)handle; (void)symbol; return (void *)0; }
char *dlerror(void) { return "Dynamic linking not supported"; }

extern i64 xiu_mprotect(void *addr, usize len, int prot);
int mprotect(void *addr, usize len, int prot) {
  return (int)xiu_mprotect(addr, len, prot);
}

float strtof(const char *nptr, char **endptr) {
  return (float)strtod(nptr, endptr);
}

long double strtold(const char *nptr, char **endptr) {
  return (long double)strtod(nptr, endptr);
}

int setpriority(int which, id_t who, int prio) {
  (void)which; (void)who; (void)prio;
  return 0;
}


int socketpair(int domain, int type, int protocol, int sv[2]) {
  (void)domain; (void)type; (void)protocol;
  return pipe(sv);
}

int utimensat(int fd, const char *path, const struct timespec times[2], int flag) {
  (void)fd; (void)path; (void)times; (void)flag;
  return 0;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
  char *s;
  if (str) s = str;
  else if (saveptr && *saveptr) s = *saveptr;
  else return NULL;

  while (*s && strchr(delim, *s)) s++;
  if (*s == '\0') {
    if (saveptr) *saveptr = NULL;
    return NULL;
  }
  char *token = s;
  while (*s && !strchr(delim, *s)) s++;
  if (*s) {
    *s = '\0';
    if (saveptr) *saveptr = s + 1;
  } else {
    if (saveptr) *saveptr = NULL;
  }
  return token;
}

int getentropy(void *buf, size_t buflen) {
  if (!buf || buflen > 256) return -1;
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    read(fd, buf, buflen);
    close(fd);
  } else {
    unsigned char *p = (unsigned char *)buf;
    static unsigned long seed = 0x123456789abcdef0;
    for (size_t i = 0; i < buflen; i++) {
      seed = seed * 6364136223846793005ULL + 1;
      p[i] = (unsigned char)(seed >> 32);
    }
  }
  return 0;
}

int fchdir(int fd) {
  (void)fd;
  return 0;
}

ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream) {
  if (!lineptr || !n || !stream) return -1;
  if (!*lineptr || *n == 0) {
    *n = 128;
    *lineptr = (char *)malloc(*n);
    if (!*lineptr) return -1;
  }
  size_t idx = 0;
  int c;
  while ((c = fgetc(stream)) != EOF) {
    if (idx + 2 >= *n) {
      size_t new_n = *n * 2;
      char *new_ptr = (char *)realloc(*lineptr, new_n);
      if (!new_ptr) return -1;
      *lineptr = new_ptr;
      *n = new_n;
    }
    (*lineptr)[idx++] = (char)c;
    if (c == delim) break;
  }
  if (idx == 0 && c == EOF) return -1;
  (*lineptr)[idx] = '\0';
  return (ssize_t)idx;
}

int chroot(const char *dirname) {
  (void)dirname;
  return 0;
}

void closefrom(int lowfd) {
  for (int fd = lowfd; fd < 256; fd++) {
    close(fd);
  }
}

void explicit_bzero(void *b, size_t len) {
  volatile unsigned char *p = (volatile unsigned char *)b;
  while (len--) *p++ = 0;
}

void freezero(void *ptr, size_t size) {
  if (ptr) {
    explicit_bzero(ptr, size);
    free(ptr);
  }
}

int futimens(int fd, const struct timespec times[2]) {
  (void)fd; (void)times;
  return 0;
}

extern i64 xiu_getpgid(pid_t pid);
extern i64 xiu_getsid(pid_t pid);

pid_t getpgid(pid_t pid) {
  return (pid_t)xiu_getpgid(pid);
}

pid_t getsid(pid_t pid) {
  return (pid_t)xiu_getsid(pid);
}


const char *const sudo_sys_signame[32] = {
    "ZERO", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "EMT",
    "FPE", "KILL", "BUS", "SEGV", "SYS", "PIPE", "ALRM", "TERM",
    "URG", "STOP", "TSTP", "CONT", "CHLD", "TTIN", "TTOU", "IO",
    "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "INFO", "USR1", "USR2"
};

const char *sudo_gai_strerror(int ecode) {
  (void)ecode;
  return "Unknown error";
}

char *getenv(const char *name) {
  if (!name || !environ) return NULL;
  usize len = strlen(name);
  for (char **ep = environ; *ep; ep++) {
    if (strncmp(*ep, name, len) == 0 && (*ep)[len] == '=') {
      return *ep + len + 1;
    }
  }
  return NULL;
}

int setenv(const char *name, const char *value, int overwrite) {
  if (!name || name[0] == '\0' || strchr(name, '=')) return -1;
  if (!value) value = "";

  usize name_len = strlen(name);
  usize val_len = strlen(value);
  usize entry_len = name_len + 1 + val_len + 1;

  if (environ) {
    for (usize i = 0; environ[i] != NULL; i++) {
      if (strncmp(environ[i], name, name_len) == 0 && environ[i][name_len] == '=') {
        if (!overwrite) return 0;
        char *new_entry = (char *)malloc(entry_len);
        if (!new_entry) return -1;
        memcpy(new_entry, name, name_len);
        new_entry[name_len] = '=';
        strcpy(new_entry + name_len + 1, value);
        environ[i] = new_entry;
        return 0;
      }
    }
  }

  usize count = 0;
  if (environ) {
    while (environ[count] != NULL) count++;
  }

  if (!s_environ_allocated || count + 2 > s_environ_capacity) {
    usize new_cap = (s_environ_capacity > 0) ? (s_environ_capacity * 2) : 64;
    char **new_env = (char **)malloc(sizeof(char *) * new_cap);
    if (!new_env) return -1;
    if (environ) {
      for (usize i = 0; i < count; i++) {
        new_env[i] = environ[i];
      }
    }
    environ = new_env;
    s_environ_allocated = 1;
    s_environ_capacity = new_cap;
  }

  char *new_entry = (char *)malloc(entry_len);
  if (!new_entry) return -1;
  memcpy(new_entry, name, name_len);
  new_entry[name_len] = '=';
  strcpy(new_entry + name_len + 1, value);

  environ[count] = new_entry;
  environ[count + 1] = NULL;
  return 0;
}

int unsetenv(const char *name) {
  if (!name || name[0] == '\0' || strchr(name, '=') || !environ) return -1;
  usize name_len = strlen(name);
  for (usize i = 0; environ[i] != NULL; i++) {
    if (strncmp(environ[i], name, name_len) == 0 && environ[i][name_len] == '=') {
      usize j = i;
      while (environ[j] != NULL) {
        environ[j] = environ[j + 1];
        j++;
      }
      i--;
    }
  }
  return 0;
}

usize fread(void *ptr, usize size, usize n, FILE *f) {
  if (!f || !ptr || size == 0 || n == 0) return 0;
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : f->_file;
  i64 res = read(fd, ptr, size * n);
  if (res <= 0) return 0;
  return (usize)res / size;
}

usize fwrite(const void *ptr, usize size, usize n, FILE *f) {
  if (!f || !ptr || size == 0 || n == 0) return 0;
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : f->_file;
  i64 res = write(fd, ptr, size * n);
  if (res <= 0) return 0;
  return (usize)res / size;
}

int fputc(int c, FILE *f) {
  char ch = (char)c;
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : (f ? f->_file : 1);
  write(fd, &ch, 1);
  return c;
}

int fputs(const char *s, FILE *f) {
  if (!s) return 0;
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : (f ? f->_file : 1);
  return (int)write(fd, s, strlen(s));
}

int fseek(FILE *f, long offset, int whence) {
  if (!f) return -1;
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : f->_file;
  i64 res = lseek(fd, (i64)offset, whence);
  return (res >= 0) ? 0 : -1;
}

long ftell(FILE *f) {
  if (!f) return -1;
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : f->_file;
  return (long)lseek(fd, 0, 1);
}

void rewind(FILE *f) {
  if (f) fseek(f, 0, 0);
}

int fgetc(FILE *f) {
  if (!f) return -1;
  char c = 0;
  if (fread(&c, 1, 1, f) == 1) return (unsigned char)c;
  return -1; // eof
}

int getc(FILE *f) {
  return fgetc(f);
}

char *fgets(char *s, int size, FILE *f) {
  if (!s || size <= 0 || !f) return NULL;
  int i = 0;
  while (i < size - 1) {
    int c = fgetc(f);
    if (c < 0) {
      if (i == 0) return NULL;
      break;
    }
    s[i++] = (char)c;
    if (c == '\n') break;
  }
  s[i] = '\0';
  return s;
}

void *bsearch(const void *key, const void *base, usize nmemb, usize size,
              int (*compar)(const void *, const void *)) {
  usize l = 0, r = nmemb;
  while (l < r) {
    usize m = l + (r - l) / 2;
    const void *p = (const u8 *)base + m * size;
    int cmp = compar(key, p);
    if (cmp == 0) return (void *)p;
    if (cmp < 0) r = m;
    else l = m + 1;
  }
  return NULL;
}

double strtod(const char *nptr, char **endptr) {
  const char *p = nptr;
  while (isspace(*p)) p++;
  double sign = 1.0;
  if (*p == '-') { sign = -1.0; p++; }
  else if (*p == '+') { p++; }
  double res = 0.0;
  while (isdigit(*p)) {
    res = res * 10.0 + (*p - '0');
    p++;
  }
  if (*p == '.') {
    p++;
    double frac = 0.1;
    while (isdigit(*p)) {
      res += (*p - '0') * frac;
      frac *= 0.1;
      p++;
    }
  }
  if (endptr) *endptr = (char *)p;
  return sign * res;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
  return (unsigned long)strtoull(nptr, endptr, base);
}

int fflush(FILE *f) { (void)f; return 0; }
int getrlimit(int res, struct rlimit *rl) { if (rl) { rl->rlim_cur = 0; rl->rlim_max = 0; } return -1; }
int setrlimit(int res, const struct rlimit *rl) { return -1; }



usize strcspn(const char *s, const char *reject) {
  usize n = 0;
  while (s[n]) {
    if (strchr(reject, s[n])) break;
    n++;
  }
  return n;
}

char *strtok(char *str, const char *delim) {
  static char *last = NULL;
  if (str) last = str;
  if (!last) return NULL;
  while (*last && strchr(delim, *last)) last++;
  if (!*last) return NULL;
  char *start = last;
  while (*last && !strchr(delim, *last)) last++;
  if (*last) *last++ = '\0';
  else last = NULL;
  return start;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
  const char *p = nptr;
  while (isspace(*p)) p++;
  if (base == 0) {
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    else if (p[0] == '0') { base = 8; p += 1; }
    else base = 10;
  } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
  }
  u64 res = 0;
  while (*p) {
    int v = -1;
    if (*p >= '0' && *p <= '9') v = *p - '0';
    else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
    if (v < 0 || v >= base) break;
    res = res * base + v;
    p++;
  }
  if (endptr) *endptr = (char *)p;
  return res;
}

long long strtoll(const char *nptr, char **endptr, int base) {
  long long res = 0;
  const char *p = nptr;
  while (*p == ' ' || *p == '\t') p++;
  int sign = 1;
  if (*p == '-') { sign = -1; p++; }
  else if (*p == '+') p++;
  while (*p >= '0' && *p <= '9') {
    res = res * base + (*p - '0');
    p++;
  }
  if (endptr) *endptr = (char *)p;
  return res * sign;
}

long strtol(const char *nptr, char **endptr, int base) {
  return (long)strtoll(nptr, endptr, base);
}

char *stpcpy(char *dest, const char *src) {
  while ((*dest++ = *src++));
  return dest - 1;
}

char *strcat(char *dest, const char *src) {
  char *p = dest + strlen(dest);
  while ((*p++ = *src++));
  return dest;
}

char *strncat(char *dest, const char *src, usize n) {
  char *p = dest + strlen(dest);
  while (n-- && *src) *p++ = *src++;
  *p = '\0';
  return dest;
}

char *realpath(const char *path, char *resolved_path) {
  if (!path) return NULL;
  if (!resolved_path) resolved_path = (char *)malloc(1024);
  if (!resolved_path) return NULL;
  strncpy(resolved_path, path, 1023);
  resolved_path[1023] = '\0';
  return resolved_path;
}

int remove(const char *pathname) {
  return unlink(pathname);
}

int execvp(const char *file, char *const argv[]) {
  if (!file || file[0] == '\0') return -1;

  // 1. If path contains '/', execute directly
  if (strchr(file, '/')) {
    return execve(file, argv, NULL);
  }

  // 2. Search through PATH components
  const char *path = getenv("PATH");
  if (!path || path[0] == '\0') {
    path = "/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin";
  }

  char full_path[512];
  const char *p = path;
  while (*p) {
    const char *sep = strchr(p, ':');
    size_t dir_len = sep ? (size_t)(sep - p) : strlen(p);

    if (dir_len > 0 && dir_len < sizeof(full_path) - 2 - strlen(file)) {
      memcpy(full_path, p, dir_len);
      full_path[dir_len] = '/';
      strcpy(full_path + dir_len + 1, file);

      execve(full_path, argv, NULL);
    }

    if (!sep) break;
    p = sep + 1;
  }

  return -1;
}

char *stpncpy(char *dest, const char *src, usize n) {
  usize i;
  for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
  char *res = dest + i;
  for (; i < n; i++) dest[i] = '\0';
  return res;
}

int killpg(pid_t pgrp, int sig) { return kill(-pgrp, sig); }


static void swap_bytes(char *a, char *b, usize size) {
    while (size--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

void qsort(void *base, usize nmemb, usize size, int (*compar)(const void *, const void *)) {
    if (!base || nmemb < 2 || size == 0) return;
    char *arr = (char *)base;

    if (nmemb <= 7) {
        for (usize i = 1; i < nmemb; i++) {
            for (usize j = i; j > 0 && compar(arr + (j - 1) * size, arr + j * size) > 0; j--) {
                swap_bytes(arr + (j - 1) * size, arr + j * size, size);
            }
        }
        return;
    }

    usize mid = nmemb / 2;
    if (compar(arr, arr + mid * size) > 0) swap_bytes(arr, arr + mid * size, size);
    if (compar(arr, arr + (nmemb - 1) * size) > 0) swap_bytes(arr, arr + (nmemb - 1) * size, size);
    if (compar(arr + mid * size, arr + (nmemb - 1) * size) > 0) swap_bytes(arr + mid * size, arr + (nmemb - 1) * size, size);

    swap_bytes(arr + mid * size, arr + (nmemb - 1) * size, size);
    char *pivot = arr + (nmemb - 1) * size;

    usize i = 0;
    for (usize j = 0; j < nmemb - 1; j++) {
        if (compar(arr + j * size, pivot) <= 0) {
            if (i != j) swap_bytes(arr + i * size, arr + j * size, size);
            i++;
        }
    }
    swap_bytes(arr + i * size, arr + (nmemb - 1) * size, size);

    qsort(arr, i, size, compar);
    qsort(arr + (i + 1) * size, nmemb - i - 1, size, compar);
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    return (int)xiu_sigaction(signum, act, oldact);
}

sig_t signal(int signum, sig_t handler) {
    struct sigaction act, oldact;
    act.sa_handler = handler;
    act.sa_flags = 0;
    act.sa_mask = 0;
    if (xiu_sigaction(signum, &act, &oldact) < 0) {
        return SIG_ERR;
    }
    return oldact.sa_handler;
}

int kill(pid_t pid, int sig) {
    return (int)xiu_kill((int)pid, sig);
}

int raise(int sig) {
    return kill(getpid(), sig);
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    return (int)xiu_sigprocmask(how, set, oldset);
}


int fstat64(int fd, struct stat64 *buf) { return fstat(fd, (struct stat *)buf); }
intmax_t strtoimax(const char *nptr, char **endptr, int base) { return (intmax_t)strtoll(nptr, endptr, base); }

int chdir(const char *path) { return (int)xiu_chdir(path); }


void *memchr(const void *s, int c, usize n) {
    const unsigned char *p = (const unsigned char *)s;
    while (n--) {
        if (*p == (unsigned char)c) return (void *)p;
        p++;
    }
    return NULL;
}

char *optarg;
int optind = 1, opterr = 1, optopt, optreset = 0;

int getopt(int argc, char * const argv[], const char *optstring) {
    static int optpos = 1;
    if (optreset || optind == 0) {
        optreset = 0;
        optind = 1;
        optpos = 1;
    }
    if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0') return -1;
    if (strcmp(argv[optind], "--") == 0) { optind++; return -1; }

    int c = argv[optind][optpos++];
    char *p = strchr(optstring, c);
    if (p == NULL || c == ':') {
        optopt = c;
        if (optstring[0] != ':' && opterr) xiu_log("Unknown option");
        if (argv[optind][optpos] == '\0') { optind++; optpos = 1; }
        return '?';
    }
    if (p[1] == ':') {
        if (argv[optind][optpos] != '\0') {
            optarg = &argv[optind][optpos];
            optind++;
            optpos = 1;
        } else if (optind + 1 < argc) {
            optind++;
            optarg = argv[optind];
            optind++;
            optpos = 1;
        } else {
            optopt = c;
            if (optstring[0] == ':') return ':';
            if (opterr) xiu_log("Option requires argument");
            optind++;
            optpos = 1;
            return '?';
        }
    } else {
        if (argv[optind][optpos] == '\0') { optind++; optpos = 1; }
    }
    return c;
}

#define _iscntrl iscntrl


// generic syscall wrapper for variadic syscalls
extern i64 xiu_syscall(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6);

int syscall(int number, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, number);
    
    u64 arg1 = __builtin_va_arg(ap, u64);
    u64 arg2 = __builtin_va_arg(ap, u64);
    u64 arg3 = __builtin_va_arg(ap, u64);
    u64 arg4 = __builtin_va_arg(ap, u64);
    u64 arg5 = __builtin_va_arg(ap, u64);
    u64 arg6 = __builtin_va_arg(ap, u64);
    
    __builtin_va_end(ap);
    
    return (long)xiu_syscall((u64)number, arg1, arg2, arg3, arg4, arg5, arg6);
}

mach_port_t mach_task_self_ = 1;

#undef mach_task_self
mach_port_t mach_task_self(void) {
    return mach_task_self_;
}

mach_msg_return_t mach_msg_trap(mach_msg_header_t *msg, mach_msg_option_t option,
                                mach_msg_size_t send_size, mach_msg_size_t rcv_size,
                                mach_port_name_t rcv_name, mach_msg_timeout_t timeout,
                                mach_port_name_t notify) {
    (void)notify;
    return (mach_msg_return_t)xiu_mach_msg(msg, (u32)option, (u32)send_size, (u32)rcv_size,
                                          (u32)rcv_name, (u32)timeout);
}

int mach_port_allocate(unsigned int task, unsigned int right, unsigned int *name) {
    return (int)xiu_mach_port_allocate((ipc_space_t)task, (mach_port_right_t)right, (mach_port_name_t *)name);
}

int mach_port_deallocate(unsigned int task, unsigned int name) {
    return (int)xiu_mach_port_deallocate((u64)task, (u64)name);
}

int mach_port_type(unsigned int task, unsigned int name, unsigned int *ptype) {
    return (int)xiu_mach_port_type((u64)task, (u64)name, (u64)ptype);
}

int mach_vm_allocate(unsigned int target, unsigned long long *address, unsigned long long size, int flags) {
    if (!address) return -1;
    unsigned int reply_port = 0;
    if (mach_port_allocate(mach_task_self(), 1, &reply_port) != 0) return -1;

    typedef struct __attribute__((packed)) {
        mach_msg_header_t hdr;
        unsigned long long address;
        unsigned long long size;
        unsigned int flags;
    } req_t;

    typedef struct __attribute__((packed)) {
        mach_msg_header_t hdr;
        unsigned int ret_code;
        unsigned long long address;
    } rep_t;

    req_t req;
    __builtin_memset(&req, 0, sizeof(req));
    req.hdr.msgh_bits = 0;
    req.hdr.msgh_size = sizeof(req);
    req.hdr.msgh_remote_port = target;
    req.hdr.msgh_local_port = reply_port;
    req.hdr.msgh_id = 4800;
    req.address = *address;
    req.size = size;
    req.flags = (unsigned int)flags;

    int rc = mach_msg_trap((mach_msg_header_t *)&req, 1, sizeof(req), 0, 0, 1000, 0);
    if (rc != 0) {
        mach_port_deallocate(mach_task_self(), reply_port);
        return rc;
    }

    rep_t rep;
    __builtin_memset(&rep, 0, sizeof(rep));
    rc = mach_msg_trap((mach_msg_header_t *)&rep, 2, 0, sizeof(rep) + 64, reply_port, 1000, 0);
    mach_port_deallocate(mach_task_self(), reply_port);
    if (rc != 0) return rc;

    *address = rep.address;
    return (int)rep.ret_code;
}

int mach_vm_deallocate(unsigned int target, unsigned long long address, unsigned long long size) {
    unsigned int reply_port = 0;
    if (mach_port_allocate(mach_task_self(), 1, &reply_port) != 0) return -1;

    typedef struct __attribute__((packed)) {
        mach_msg_header_t hdr;
        unsigned long long address;
        unsigned long long size;
    } req_t;

    typedef struct __attribute__((packed)) {
        mach_msg_header_t hdr;
        unsigned int ret_code;
    } rep_t;

    req_t req;
    __builtin_memset(&req, 0, sizeof(req));
    req.hdr.msgh_size = sizeof(req);
    req.hdr.msgh_remote_port = target;
    req.hdr.msgh_local_port = reply_port;
    req.hdr.msgh_id = 4801;
    req.address = address;
    req.size = size;

    int rc = mach_msg_trap((mach_msg_header_t *)&req, 1, sizeof(req), 0, 0, 1000, 0);
    if (rc != 0) {
        mach_port_deallocate(mach_task_self(), reply_port);
        return rc;
    }

    rep_t rep;
    __builtin_memset(&rep, 0, sizeof(rep));
    rc = mach_msg_trap((mach_msg_header_t *)&rep, 2, 0, sizeof(rep) + 64, reply_port, 1000, 0);
    mach_port_deallocate(mach_task_self(), reply_port);
    if (rc != 0) return rc;
    return (int)rep.ret_code;
}

// darwin mach bootstrap server apis

mach_port_t bootstrap_port = 1; // default root bootstrap port

extern i64 xiu_mach_register_service(const char *name, mach_port_name_t port_name);
extern i64 xiu_mach_lookup_service(const char *name, mach_port_name_t *port_out);

kern_return_t bootstrap_register(mach_port_t bp, name_t service_name, mach_port_t sp) {
    (void)bp;
    if (!service_name) return BOOTSTRAP_UNKNOWN_SERVICE;
    return (xiu_mach_register_service(service_name, (mach_port_name_t)sp) == 0) ? BOOTSTRAP_SUCCESS : BOOTSTRAP_NO_MEMORY;
}

kern_return_t bootstrap_look_up(mach_port_t bp, const name_t service_name, mach_port_t *sp) {
    (void)bp;
    if (!service_name || !sp) return BOOTSTRAP_UNKNOWN_SERVICE;
    mach_port_name_t p = 0;
    if (xiu_mach_lookup_service(service_name, &p) == 0 && p != 0) {
        *sp = (mach_port_t)p;
        return BOOTSTRAP_SUCCESS;
    }
    if (strstr(service_name, "WindowServer")) {
        if (xiu_mach_lookup_service("com.ravynos.WindowServer", &p) == 0 && p != 0) {
            *sp = (mach_port_t)p;
            return BOOTSTRAP_SUCCESS;
        }
        if (xiu_mach_lookup_service("com.apple.WindowServer", &p) == 0 && p != 0) {
            *sp = (mach_port_t)p;
            return BOOTSTRAP_SUCCESS;
        }
    }
    return BOOTSTRAP_UNKNOWN_SERVICE;
}

extern i64 xiu_sysinfo(void *info);
int sysinfo(void *info) {
    return (int)xiu_sysinfo(info);
}

// darwin bsd socket apis

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern i64 xiu_socket(u64 dom, u64 type, u64 proto);
extern i64 xiu_bind(u64 fd, u64 addr, u64 addrlen);
extern i64 xiu_connect(u64 fd, u64 addr, u64 addrlen);
extern i64 xiu_listen(u64 fd, u64 backlog);
extern i64 xiu_accept(u64 fd, u64 addr_out, u64 addrlen_out);
extern i64 xiu_sendto(u64 fd, u64 buf, u64 len, u64 flags, u64 dest_addr, u64 addrlen);
extern i64 xiu_recvfrom(u64 fd, u64 buf, u64 len, u64 flags, u64 src_addr, u64 addrlen_ptr);
extern i64 xiu_shutdown(u64 fd, u64 how);
extern i64 xiu_setsockopt(u64 fd, u64 level, u64 optname, u64 optval, u64 optlen);
extern i64 xiu_getsockopt(u64 fd, u64 level, u64 optname, u64 optval, u64 optlen_ptr);

int socket(int domain, int type, int protocol) {
    return (int)xiu_socket((u64)domain, (u64)type, (u64)protocol);
}

int bind(int socket, const struct sockaddr *address, socklen_t address_len) {
    return (int)xiu_bind((u64)socket, (u64)address, (u64)address_len);
}

int connect(int socket, const struct sockaddr *address, socklen_t address_len) {
    return (int)xiu_connect((u64)socket, (u64)address, (u64)address_len);
}

int listen(int socket, int backlog) {
    return (int)xiu_listen((u64)socket, (u64)backlog);
}

int accept(int socket, struct sockaddr *address, socklen_t *address_len) {
    return (int)xiu_accept((u64)socket, (u64)address, (u64)address_len);
}

ssize_t send(int socket, const void *buffer, size_t length, int flags) {
    return (ssize_t)xiu_sendto((u64)socket, (u64)buffer, (u64)length, (u64)flags, 0, 0);
}

ssize_t recv(int socket, void *buffer, size_t length, int flags) {
    return (ssize_t)xiu_recvfrom((u64)socket, (u64)buffer, (u64)length, (u64)flags, 0, 0);
}

ssize_t sendto(int socket, const void *buffer, size_t length, int flags,
               const struct sockaddr *dest_addr, socklen_t dest_len) {
    return (ssize_t)xiu_sendto((u64)socket, (u64)buffer, (u64)length, (u64)flags, (u64)dest_addr, (u64)dest_len);
}

ssize_t recvfrom(int socket, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_len) {
    return (ssize_t)xiu_recvfrom((u64)socket, (u64)buffer, (u64)length, (u64)flags, (u64)address, (u64)address_len);
}

int shutdown(int socket, int how) {
    return (int)xiu_shutdown((u64)socket, (u64)how);
}

int setsockopt(int socket, int level, int option_name, const void *option_value, socklen_t option_len) {
    return (int)xiu_setsockopt((u64)socket, (u64)level, (u64)option_name, (u64)option_value, (u64)option_len);
}

int getsockopt(int socket, int level, int option_name, void *option_value, socklen_t *option_len) {
    return (int)xiu_getsockopt((u64)socket, (u64)level, (u64)option_name, (u64)option_value, (u64)option_len);
}

// endianness conversions

#undef htons
#undef ntohs
#undef htonl
#undef ntohl

uint16_t htons(uint16_t v) { return (uint16_t)(((v & 0xFF) << 8) | ((v >> 8) & 0xFF)); }
uint16_t ntohs(uint16_t v) { return htons(v); }

uint32_t htonl(uint32_t v) {
    return ((v & 0xFF) << 24) | (((v >> 8) & 0xFF) << 16) |
           (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
}
uint32_t ntohl(uint32_t v) { return htonl(v); }

// ipv4 string <-> binary conversions

in_addr_t inet_addr(const char *cp) {
    if (!cp) return (in_addr_t)-1;
    uint32_t val[4] = {0};
    int idx = 0;
    const char *p = cp;

    while (*p && idx < 4) {
        if (*p >= '0' && *p <= '9') {
            val[idx] = val[idx] * 10 + (*p - '0');
        } else if (*p == '.') {
            idx++;
        } else {
            break;
        }
        p++;
    }

    if (idx != 3) return (in_addr_t)-1;
    uint32_t res = (val[0] & 0xFF) | ((val[1] & 0xFF) << 8) |
                   ((val[2] & 0xFF) << 16) | ((val[3] & 0xFF) << 24);
    return res;
}

char *inet_ntoa(struct in_addr in) {
    static char buf[32];
    uint8_t *b = (uint8_t *)&in.s_addr;
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return buf;
}

int inet_pton(int af, const char *src, void *dst) {
    if (af != AF_INET || !src || !dst) return -1;
    in_addr_t addr = inet_addr(src);
    if (addr == (in_addr_t)-1 && strcmp(src, "255.255.255.255") != 0) return 0;
    *(in_addr_t *)dst = addr;
    return 1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
    if (af != AF_INET || !src || !dst) return NULL;
    const uint8_t *b = (const uint8_t *)src;
    snprintf(dst, size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return dst;
}

#include <netdb.h>

#pragma pack(push, 1)
struct dns_hdr {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
};
#pragma pack(pop)

static void dns_encode_name(const char *src, uint8_t *dst, int *out_len) {
    int pos = 0;
    const char *p = src;
    while (*p) {
        const char *dot = strchr(p, '.');
        int len = dot ? (int)(dot - p) : (int)strlen(p);
        dst[pos++] = (uint8_t)len;
        memcpy(dst + pos, p, len);
        pos += len;
        if (!dot) break;
        p = dot + 1;
    }
    dst[pos++] = 0;
    *out_len = pos;
}

struct hostent *gethostbyname(const char *name) {
    static struct hostent he;
    static char *addr_list[2];
    static in_addr_t resolved_addr;
    static char host_buf[128];

    if (!name) return NULL;

    in_addr_t direct_ip = inet_addr(name);
    if (direct_ip != (in_addr_t)-1 || strcmp(name, "255.255.255.255") == 0) {
        resolved_addr = direct_ip;
        strncpy(host_buf, name, sizeof(host_buf) - 1);
        he.h_name = host_buf;
        he.h_aliases = NULL;
        he.h_addrtype = AF_INET;
        he.h_length = sizeof(in_addr_t);
        addr_list[0] = (char *)&resolved_addr;
        addr_list[1] = NULL;
        he.h_addr_list = addr_list;
        return &he;
    }

    if (strcmp(name, "localhost") == 0) {
        resolved_addr = inet_addr("127.0.0.1");
    } else if (strcmp(name, "gateway") == 0 || strcmp(name, "router") == 0) {
        resolved_addr = inet_addr("10.0.2.2");
    } else {
        resolved_addr = 0;
    }

    if (resolved_addr != 0) {
        strncpy(host_buf, name, sizeof(host_buf) - 1);
        he.h_name = host_buf;
        he.h_aliases = NULL;
        he.h_addrtype = AF_INET;
        he.h_length = sizeof(in_addr_t);
        addr_list[0] = (char *)&resolved_addr;
        addr_list[1] = NULL;
        he.h_addr_list = addr_list;
        return &he;
    }

    uint8_t packet[512];
    memset(packet, 0, sizeof(packet));

    struct dns_hdr *hdr = (struct dns_hdr *)packet;
    hdr->id = htons(0x4242);
    hdr->flags = htons(0x0100);
    hdr->qdcount = htons(1);

    int qname_len = 0;
    dns_encode_name(name, packet + sizeof(struct dns_hdr), &qname_len);

    uint8_t *qsuffix = packet + sizeof(struct dns_hdr) + qname_len;
    *(uint16_t *)(qsuffix) = htons(1);
    *(uint16_t *)(qsuffix + 2) = htons(1);

    int total_query_len = sizeof(struct dns_hdr) + qname_len + 4;

    static const char *dns_servers[] = {"10.0.2.3", "8.8.8.8", "1.1.1.1", NULL};

    for (int s = 0; dns_servers[s] != NULL; s++) {
        int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd < 0) continue;

        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(53);
        sin.sin_addr.s_addr = inet_addr(dns_servers[s]);

        sendto(fd, packet, total_query_len, 0, (struct sockaddr *)&sin, sizeof(sin));

        uint8_t rx[512];
        ssize_t rx_len = recv(fd, rx, sizeof(rx), 0);
        close(fd);

        if (rx_len > (ssize_t)sizeof(struct dns_hdr)) {
            struct dns_hdr *res_hdr = (struct dns_hdr *)rx;
            uint16_t ancount = ntohs(res_hdr->ancount);
            if (ancount > 0) {
                uint8_t *ptr = rx + sizeof(struct dns_hdr);
                while (ptr < rx + rx_len && *ptr != 0) {
                    if ((*ptr & 0xC0) == 0xC0) {
                        ptr += 2;
                        break;
                    }
                    ptr += (*ptr + 1);
                }
                if (*ptr == 0) ptr++;
                ptr += 4;
                for (int a = 0; a < ancount && ptr + 12 <= rx + rx_len; a++) {
                    if ((*ptr & 0xC0) == 0xC0) {
                        ptr += 2;
                    } else {
                        while (ptr < rx + rx_len && *ptr != 0) ptr += (*ptr + 1);
                        if (ptr < rx + rx_len && *ptr == 0) ptr++;
                    }

                    if (ptr + 10 > rx + rx_len) break;

                    uint16_t type = ntohs(*(uint16_t *)ptr);
                    uint16_t rdlen = ntohs(*(uint16_t *)(ptr + 8));
                    ptr += 10;

                    if (type == 1 && rdlen == 4 && ptr + 4 <= rx + rx_len) {
                        memcpy(&resolved_addr, ptr, 4);
                        strncpy(host_buf, name, sizeof(host_buf) - 1);
                        he.h_name = host_buf;
                        he.h_aliases = NULL;
                        he.h_addrtype = AF_INET;
                        he.h_length = sizeof(in_addr_t);
                        addr_list[0] = (char *)&resolved_addr;
                        addr_list[1] = NULL;
                        he.h_addr_list = addr_list;
                        return &he;
                    }
                    ptr += rdlen;
                }
            }
        }
    }

    return NULL;
}

int getpagesize(void) {
    return 4096;
}

int vasprintf(char **ret, const char *format, va_list ap) {
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int len = vsnprintf(NULL, 0, format, ap_copy);
    va_end(ap_copy);
    if (len < 0) {
        *ret = NULL;
        return -1;
    }
    *ret = (char *)malloc(len + 1);
    if (!*ret) return -1;
    return vsnprintf(*ret, len + 1, format, ap);
}

int asprintf(char **ret, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vasprintf(ret, format, ap);
    va_end(ap);
    return res;
}

int __getlogin(char *name, int namelen) {
    if (!name || namelen <= 0) {
        errno = EINVAL;
        return -1;
    }
    i64 ret = xiu_getlogin(name, (usize)namelen);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int __setlogin(const char *name) {
    if (!name) {
        errno = EINVAL;
        return -1;
    }
    i64 ret = xiu_setlogin(name);
    if (ret < 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int kqueue(void) {
    return dup(0);
}

int kevent(int kq, const struct kevent *changelist, int nchanges, struct kevent *eventlist, int nevents, const struct timespec *timeout) {
    (void)kq; (void)changelist; (void)nchanges; (void)eventlist; (void)nevents; (void)timeout;
    return 0;
}

kvm_t *kvm_open(const char *execfile, const char *corefile, const char *swapfile, int flags, const char *errstr) {
    (void)execfile; (void)corefile; (void)swapfile; (void)flags; (void)errstr;
    static int dummy_kd = 1;
    return (kvm_t *)&dummy_kd;
}

int kvm_close(kvm_t *kd) {
    (void)kd;
    return 0;
}

typedef struct {
  u32 pid;
  u32 ppid;
  u32 state;
  u32 thread_count;
  char name[32];
} xiu_procinfo_t;

extern i64 xiu_proclist(xiu_procinfo_t *buf, u64 max_count);

struct kinfo_proc *kvm_getprocs(kvm_t *kd, int op, int arg, int *cnt) {
    (void)kd; (void)op; (void)arg;
    static struct kinfo_proc s_kinfo_procs[64];
    xiu_procinfo_t raw_procs[64];
    int n = (int)xiu_proclist(raw_procs, 64);
    if (n < 0) n = 0;

    memset(s_kinfo_procs, 0, sizeof(s_kinfo_procs));
    for (int i = 0; i < n; i++) {
        s_kinfo_procs[i].ki_pid = (pid_t)raw_procs[i].pid;
        s_kinfo_procs[i].ki_ppid = (pid_t)raw_procs[i].ppid;
        s_kinfo_procs[i].ki_uid = 0;
        s_kinfo_procs[i].ki_rgid = 0;
        s_kinfo_procs[i].ki_stat = (char)raw_procs[i].state;
        strncpy(s_kinfo_procs[i].ki_comm, raw_procs[i].name, sizeof(s_kinfo_procs[i].ki_comm) - 1);
    }
    if (cnt) *cnt = n;
    return s_kinfo_procs;
}



double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (y == 1.0) return x;
    if (x == 0.0) return 0.0;
    double res = 1.0;
    long long exp = (long long)y;
    double base = x;
    if (exp < 0) {
        base = 1.0 / base;
        exp = -exp;
    }
    while (exp > 0) {
        if (exp & 1) res *= base;
        base *= base;
        exp >>= 1;
    }
    return res;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
    (void)attr;
    i64 pid = xiu_fork();
    if (pid == 0) {
        start_routine(arg);
        xiu_exit(0);
    }
    if (thread) *thread = (pthread_t)(uintptr_t)pid;
    return (pid < 0) ? -1 : 0;
}

int pthread_cancel(pthread_t thread) {
    (void)thread;
    return 0;
}

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine) return -1;
    if (once_control->__sig == 0x30B1BCBA) return 0;
    init_routine();
    once_control->__sig = 0x30B1BCBA;
    return 0;
}


#define PTHREAD_KEYS_MAX 64

static void *s_pthread_tls[PTHREAD_KEYS_MAX];
static int s_pthread_key_count = 1;

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    (void)destructor;
    if (!key) return -1;
    if (s_pthread_key_count >= PTHREAD_KEYS_MAX) return -1;
    *key = s_pthread_key_count++;
    s_pthread_tls[*key] = NULL;
    return 0;
}

int pthread_key_delete(pthread_key_t key) {
    if (key >= 0 && key < PTHREAD_KEYS_MAX) {
        s_pthread_tls[key] = NULL;
    }
    return 0;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key >= 0 && key < PTHREAD_KEYS_MAX) {
        s_pthread_tls[key] = (void *)value;
        return 0;
    }
    return -1;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key >= 0 && key < PTHREAD_KEYS_MAX) {
        return s_pthread_tls[key];
    }
    return NULL;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    (void)mutex; (void)attr;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    (void)cond; (void)attr;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    (void)cond; (void)mutex;
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
    const struct timespec *abstime) {
    (void)cond; (void)mutex; (void)abstime;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

extern i64 xiu_fsync(u64 fd);
int fsync(int fd) {
    i64 ret = xiu_fsync((u64)fd);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return 0;
}

int getdtablesize(void) {
    return 1024;
}

extern i64 xiu_getsockname(u64 fd, u64 addr, u64 len);
int getsockname(int fd, struct sockaddr *addr, socklen_t *len) {
    if (!addr || !len) { errno = EFAULT; return -1; }
    i64 ret = xiu_getsockname((u64)fd, (u64)addr, (u64)len);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

int shm_open(const char *name, int oflag, ...) {
    if (!name) return -1;
    mode_t mode = 0;
    if (oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    char path[256];
    if (name[0] == '/') snprintf(path, sizeof(path), "/tmp%s", name);
    else snprintf(path, sizeof(path), "/tmp/%s", name);

    char *p = path + 5;
    while (*p) {
        if (*p == '/') *p = '_';
        p++;
    }
    return open(path, oflag, mode ? mode : 0666);
}

int shm_unlink(const char *name) {
    if (!name) return -1;
    char path[256];
    if (name[0] == '/') snprintf(path, sizeof(path), "/tmp%s", name);
    else snprintf(path, sizeof(path), "/tmp/%s", name);

    char *p = path + 5;
    while (*p) {
        if (*p == '/') *p = '_';
        p++;
    }
    return unlink(path);
}

int shmget(key_t key, size_t size, int shmflg) {
    (void)shmflg;
    char path[64];
    snprintf(path, sizeof(path), "/tmp/shm_%d", (int)key);
    int fd = open(path, O_RDWR | O_CREAT, 0666);
    if (fd >= 0 && size > 0) ftruncate(fd, (off_t)size);
    return fd;
}

void *shmat(int shmid, const void *shmaddr, int shmflg) {
    (void)shmflg;
    struct stat st;
    if (fstat(shmid, &st) < 0 || st.st_size == 0) return (void *)-1;
    return mmap((void *)shmaddr, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shmid, 0);
}

int shmdt(const void *shmaddr) {
    (void)shmaddr;
    return 0;
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf) {
    (void)shmid; (void)cmd; (void)buf;
    return 0;
}

int system(const char *string) {
    if (!string) return 1;
    i64 pid = xiu_fork();
    if (pid == 0) {
        char *argv[] = { "/bin/sh", "-c", (char *)string, NULL };
        xiu_execve("/bin/sh", argv, NULL);
        xiu_exit(127);
    }
    if (pid < 0) return -1;
    int status = 0;
    xiu_wait4((int)pid, &status, 0, NULL);
    return status;
}

pid_t wait(int *stat_loc) {
    return (pid_t)xiu_wait4(-1, stat_loc, 0, NULL);
}

char *strndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t len = 0;
    while (len < n && s[len] != '\0') len++;
    char *res = (char *)malloc(len + 1);
    if (!res) return NULL;
    memcpy(res, s, len);
    res[len] = '\0';
    return res;
}

void _exit(int status) {
    while (1) {
        xiu_exit((i64)status);
    }
}

wctype_t wctype(const char *name) {
    (void)name;
    return (wctype_t)1;
}

int execl(const char *path, const char *arg0, ...) {
    char *argv[64];
    argv[0] = (char *)arg0;
    int argc = 1;
    if (arg0 != NULL) {
        va_list ap;
        va_start(ap, arg0);
        while (argc < 63) {
            char *arg = va_arg(ap, char *);
            if (!arg) break;
            argv[argc++] = arg;
        }
        va_end(ap);
    } else {
        argc = 0;
    }
    argv[argc] = NULL;
    extern char **environ;
    return execve(path, argv, environ);
}

int execle(const char *path, const char *arg0, ...) {
    char *argv[64];
    argv[0] = (char *)arg0;
    int argc = 1;
    char **envp = NULL;
    if (arg0 != NULL) {
        va_list ap;
        va_start(ap, arg0);
        while (argc < 63) {
            char *arg = va_arg(ap, char *);
            if (!arg) {
                envp = va_arg(ap, char **);
                break;
            }
            argv[argc++] = arg;
        }
        va_end(ap);
    } else {
        argc = 0;
    }
    argv[argc] = NULL;
    return execve(path, argv, envp);
}

kern_return_t bootstrap_check_in(mach_port_t bp, const name_t service_name, mach_port_t *sp) {
    (void)bp;
    if (!service_name || !sp) return BOOTSTRAP_UNKNOWN_SERVICE;
    mach_port_t port = 0;
    if (mach_port_allocate(mach_task_self(), 1 /* MACH_PORT_RIGHT_RECEIVE */, &port) != 0) {
        return BOOTSTRAP_NO_MEMORY;
    }
    *sp = port;
    if (xiu_mach_register_service(service_name, (mach_port_name_t)port) != 0) {
        return BOOTSTRAP_NO_MEMORY;
    }
    if (strstr(service_name, "WindowServer")) {
        xiu_mach_register_service("com.apple.WindowServer", (mach_port_name_t)port);
        xiu_mach_register_service("com.ravynos.WindowServer", (mach_port_name_t)port);
    }
    return BOOTSTRAP_SUCCESS;
}

mach_msg_return_t mach_msg(mach_msg_header_t *msg, mach_msg_option_t option, mach_msg_size_t send_size, mach_msg_size_t rcv_size, mach_port_name_t rcv_name, mach_msg_timeout_t timeout, mach_port_name_t notify) {
    (void)notify;
    return (mach_msg_return_t)xiu_mach_msg(msg, option, send_size, rcv_size, rcv_name, timeout);
}

kern_return_t mach_port_insert_right(ipc_space_t task, mach_port_name_t name, mach_port_t poly, mach_msg_type_name_t polyPoly) {
    (void)task; (void)name; (void)poly; (void)polyPoly;
    return 0;
}

char *nl_langinfo(int item) {
    switch (item) {
        case 0: return "UTF-8"; // CODESET
        case 1: return "%a %b %e %H:%M:%S %Y"; // D_T_FMT
        case 2: return "%m/%d/%y"; // D_FMT
        case 3: return "%H:%M:%S"; // T_FMT
        case 4: return "%I:%M:%S %p"; // T_FMT_AMPM
        case 5: return "AM"; // AM_STR
        case 6: return "PM"; // PM_STR
        case 7: return "Sunday"; // DAY_1
        case 8: return "Monday"; // DAY_2
        case 9: return "Tuesday"; // DAY_3
        case 10: return "Wednesday"; // DAY_4
        case 11: return "Thursday"; // DAY_5
        case 12: return "Friday"; // DAY_6
        case 13: return "Saturday"; // DAY_7
        case 14: return "Sun"; // ABDAY_1
        case 15: return "Mon"; // ABDAY_2
        case 16: return "Tue"; // ABDAY_3
        case 17: return "Wed"; // ABDAY_4
        case 18: return "Thu"; // ABDAY_5
        case 19: return "Fri"; // ABDAY_6
        case 20: return "Sat"; // ABDAY_7
        case 21: return "January"; // MON_1
        case 22: return "February"; // MON_2
        case 23: return "March"; // MON_3
        case 24: return "April"; // MON_4
        case 25: return "May"; // MON_5
        case 26: return "June"; // MON_6
        case 27: return "July"; // MON_7
        case 28: return "August"; // MON_8
        case 29: return "September"; // MON_9
        case 30: return "October"; // MON_10
        case 31: return "November"; // MON_11
        case 32: return "December"; // MON_12
        case 33: return "Jan"; // ABMON_1
        case 34: return "Feb"; // ABMON_2
        case 35: return "Mar"; // ABMON_3
        case 36: return "Apr"; // ABMON_4
        case 37: return "May"; // ABMON_5
        case 38: return "Jun"; // ABMON_6
        case 39: return "Jul"; // ABMON_7
        case 40: return "Aug"; // ABMON_8
        case 41: return "Sep"; // ABMON_9
        case 42: return "Oct"; // ABMON_10
        case 43: return "Nov"; // ABMON_11
        case 44: return "Dec"; // ABMON_12
        case 50: return "."; // RADIXCHAR
        case 51: return ","; // THOUSEP
        case 52: return "^[yY]"; // YESEXPR
        case 53: return "^[nN]"; // NOEXPR
        default: return "";
    }
}

double sin(double x) {
    double res;
    __asm__ __volatile__ ("fsin" : "=t" (res) : "0" (x));
    return res;
}

double cos(double x) {
    double res;
    __asm__ __volatile__ ("fcos" : "=t" (res) : "0" (x));
    return res;
}

double fmod(double x, double y) {
    double res;
    __asm__ __volatile__ ("1: fprem; fnstsw %%ax; sahf; jp 1b" : "=t" (res) : "0" (x), "u" (y) : "ax", "cc");
    return res;
}

double acos(double x) {
    if (x < -1.0) x = -1.0;
    if (x > 1.0) x = 1.0;
    double s = 1.0 - x * x;
    double sq;
    __asm__ __volatile__ ("fsqrt" : "=t" (sq) : "0" (s));
    double res;
    __asm__ __volatile__ ("fpatan" : "=t" (res) : "0" (x), "u" (sq) : "st(1)");
    return res;
}

double __exp10(double x) {
    return __builtin_pow(10.0, x);
}

float __exp10f(float x) {
    return __builtin_powf(10.0f, x);
}

double atan2(double y, double x) {
    double res;
    __asm__ __volatile__ ("fpatan" : "=t" (res) : "0" (y), "u" (x) : "st(1)");
    return res;
}

double hypot(double x, double y) {
    return __builtin_sqrt(x * x + y * y);
}

long lroundf(float x) {
    return (long)(x >= 0.0f ? x + 0.5f : x - 0.5f);
}

struct __sincos_res {
    double s;
    double c;
};

struct __sincos_res __sincos_stret(double x) {
    struct __sincos_res res;
    __asm__ __volatile__ ("fsincos" : "=t" (res.c), "=u" (res.s) : "0" (x));
    return res;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    (void)clk_id;
    if (!tp) return -1;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tp->tv_sec = tv.tv_sec;
    tp->tv_nsec = tv.tv_usec * 1000;
    return 0;
}

double exp(double x) {
    return __builtin_exp(x);
}

float fmodf(float x, float y) {
    return __builtin_fmodf(x, y);
}

pthread_t pthread_self(void) {
    return (pthread_t)1;
}

int pthread_getschedparam(pthread_t t, int *p, struct sched_param *param) {
    (void)t; (void)p; (void)param;
    return 0;
}

int pthread_setschedparam(pthread_t t, int p, const struct sched_param *param) {
    (void)t; (void)p; (void)param;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m) {
    (void)m;
    return 0;
}

int sched_get_priority_min(int p) {
    (void)p;
    return 0;
}

__attribute__((weak)) void _NSInitializeSynchronizedDirective(void) {
}

double log10(double x) {
    return __builtin_log10(x);
}

int fpclassify(double x) {
    return __builtin_fpclassify(0, 1, 2, 3, 4, x);
}

struct lconv *localeconv(void) {
    static struct lconv lc = {
        .decimal_point = ".",
        .thousands_sep = ",",
        .grouping = "\3",
        .int_curr_symbol = "USD ",
        .currency_symbol = "$",
        .mon_decimal_point = ".",
        .mon_thousands_sep = ",",
        .mon_grouping = "\3",
        .positive_sign = "",
        .negative_sign = "-",
        .int_frac_digits = 2,
        .frac_digits = 2,
        .p_cs_precedes = 1,
        .p_sep_by_space = 0,
        .n_cs_precedes = 1,
        .n_sep_by_space = 0,
        .p_sign_posn = 1,
        .n_sign_posn = 1,
        .int_p_cs_precedes = 1,
        .int_n_cs_precedes = 1,
        .int_p_sep_by_space = 0,
        .int_n_sep_by_space = 0,
        .int_p_sign_posn = 1,
        .int_n_sign_posn = 1
    };
    return &lc;
}

struct lconv *localeconv_l(void *loc) {
    (void)loc;
    return localeconv();
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type) {
    (void)addr; (void)len; (void)type;
    return NULL;
}

int signbit(double x) {
    return __builtin_signbit(x);
}

static char *s_default_env[] = { "PATH=/bin:/usr/bin", "HOME=/", "USER=root", NULL };
char **NSPlatform_environ(void) {
    return s_default_env;
}


const void *kCGImageDestinationDPI = "kCGImageDestinationDPI";
const void *kCGImageDestinationLossyCompressionQuality = "kCGImageDestinationLossyCompressionQuality";
const void *kCGImagePropertyDPIHeight = "kCGImagePropertyDPIHeight";
const void *kCGImagePropertyDPIWidth = "kCGImagePropertyDPIWidth";

#include "include/mach-o/loader.h"
extern const struct mach_header_64 _mh_execute_header __attribute__((weak));

char *getsectdata(const char *segname, const char *sectname, unsigned long *size) {
    if (!sectname) return NULL;
    const struct mach_header_64 *hdr = (const struct mach_header_64 *)0x1000ULL;
    if (hdr->magic != MH_MAGIC_64) {
        hdr = (const struct mach_header_64 *)0x100000000ULL;
        if (hdr->magic != MH_MAGIC_64) {
            hdr = &_mh_execute_header;
            if (!hdr || hdr->magic != MH_MAGIC_64) return NULL;
        }
    }
    
    const uint8_t *cmd = (const uint8_t *)(hdr + 1);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            const struct section_64 *sec = (const struct section_64 *)(seg + 1);
            for (uint32_t s = 0; s < seg->nsects; s++) {
                if (strncmp(sec[s].sectname, sectname, 16) == 0) {
                    if (!segname || segname[0] == '\0' || strncmp(seg->segname, segname, 16) == 0) {
                        if (size) *size = sec[s].size;
                        return (char *)(sec[s].addr);
                    }
                }
            }
        }
        cmd += lc->cmdsize;
    }
    return NULL;
}

const char *const sys_signame[32] = {
    "0", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "EMT",
    "FPE", "KILL", "BUS", "SEGV", "SYS", "PIPE", "ALRM", "TERM",
    "URG", "STOP", "TSTP", "CONT", "CHLD", "TTIN", "TTOU", "IO",
    "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "INFO", "USR1", "USR2"
};

long long strtonum(const char *numstr, long long minval, long long maxval, const char **errstrp) {
    if (minval > maxval) {
        if (errstrp) *errstrp = "invalid";
        return 0;
    }
    char *ep = NULL;
    errno = 0;
    long long ll = strtoll(numstr, &ep, 10);
    if (numstr == ep || *ep != '\0') {
        if (errstrp) *errstrp = "invalid";
        return 0;
    }
    if (errno == ERANGE || ll < minval || ll > maxval) {
        if (errstrp) *errstrp = (ll < minval) ? "too small" : "too large";
        return 0;
    }
    if (errstrp) *errstrp = NULL;
    return ll;
}

char *strsep(char **stringp, const char *delim) {
    if (!stringp || !*stringp) return NULL;
    char *s = *stringp;
    char *end = s + strcspn(s, delim);
    if (*end) {
        *end = '\0';
        *stringp = end + 1;
    } else {
        *stringp = NULL;
    }
    return s;
}

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

int getrusage(int who, struct rusage *usage) {

    (void)who;
    if (!usage) return -1;
    memset(usage, 0, sizeof(*usage));
    xiu_sysinfo_t si;
    if (xiu_sysinfo(&si) == 0) {
        usage->ru_utime.tv_sec = si.uptime_seconds / 2;
        usage->ru_utime.tv_usec = 0;
        usage->ru_stime.tv_sec = si.uptime_seconds / 2;
        usage->ru_stime.tv_usec = 0;
        usage->ru_maxrss = (long)((si.total_memory - si.free_memory) / 1024);
    }
    return 0;
}


pid_t wait3(int *status, int options, struct rusage *rusage) {
    if (rusage) memset(rusage, 0, sizeof(*rusage));
    return waitpid(-1, status, options);
}

void *memccpy(void *dst, const void *src, int c, size_t n) {
    const unsigned char *s = (const unsigned char *)src;
    unsigned char *d = (unsigned char *)dst;
    unsigned char uc = (unsigned char)c;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
        if (s[i] == uc) return &d[i + 1];
    }
    return NULL;
}


size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t srclen = strlen(src);
    if (size > 0) {
        size_t copylen = (srclen >= size) ? size - 1 : srclen;
        memcpy(dst, src, copylen);
        dst[copylen] = '\0';
    }
    return srclen;
}

uintmax_t strtoumax(const char *nptr, char **endptr, int base) {
    return (uintmax_t)strtoull(nptr, endptr, base);
}



FILE *funopen(const void *cookie,
              int (*readfn)(void *, char *, int),
              int (*writefn)(void *, const char *, int),
              fpos_t (*seekfn)(void *, fpos_t, int),
              int (*closefn)(void *)) {
    (void)cookie; (void)readfn; (void)writefn; (void)seekfn; (void)closefn;
    return stdout;
}

int __mb_cur_max = 1;
int ___mb_cur_max(void) { return 1; }
int ___mb_cur_max_l(void *loc) { (void)loc; return 1; }

unsigned int alarm(unsigned int seconds) {
    (void)seconds;
    return 0;
}

void clearerr(FILE *f) {
    (void)f;
}

char *cuserid(char *s) {
    if (s) {
        strcpy(s, "root");
        return s;
    }
    return "root";
}

int feof(FILE *f) {
    (void)f;
    return 0;
}

int ferror(FILE *f) {
    (void)f;
    return 0;
}

int fileno(FILE *f) {
    if (!f) return -1;
    if (f == stdin) return 0;
    if (f == stdout) return 1;
    if (f == stderr) return 2;
    return 0;
}

int isinf(double x) {
    return __builtin_isinf(x);
}

int isnan(double x) {
    return __builtin_isnan(x);
}

int link(const char *p1, const char *p2) {
    (void)p1; (void)p2;
    return -1;
}

char *mktemp(char *template) {
    return template;
}

int putc(int c, FILE *stream) {
    return fputc(c, stream);
}

static unsigned long s_next_rand = 1;
int rand(void) {
    s_next_rand = s_next_rand * 1103515245 + 12345;
    return (unsigned int)(s_next_rand / 65536) % 32768;
}

void srand(unsigned int seed) {
    s_next_rand = seed;
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    (void)path; (void)buf; (void)bufsiz;
    return -1;
}

int setresuid(uid_t r, uid_t e, uid_t s) {
    (void)r; (void)e; (void)s;
    return 0;
}

int setresgid(gid_t r, gid_t e, gid_t s) {
    (void)r; (void)e; (void)s;
    return 0;
}

int setvbuf(FILE *f, char *buf, int mode, size_t size) {
    (void)f; (void)buf; (void)mode; (void)size;
    return 0;
}

int symlink(const char *p1, const char *p2) {
    (void)p1; (void)p2;
    return -1;
}

char *ttyname(int fd) {
    (void)fd;
    return "/dev/tty";
}


/* -----------------------------------------------------------------------------
 * Termcap / Terminfo Emulation for Darwin / XIU Console
 * ----------------------------------------------------------------------------- */
int tgetent(char *bp, const char *name) {
    (void)bp; (void)name;
    return 1;
}

int tgetflag(const char *id) {
    if (!id) return 0;
    if (strcmp(id, "am") == 0) return 1; /* auto margins */
    if (strcmp(id, "xn") == 0) return 1; /* newline glitch */
    if (strcmp(id, "hs") == 0) return 0; /* status line */
    return 0;
}

int tgetnum(const char *id) {
    if (!id) return -1;
    if (strcmp(id, "co") == 0 || strcmp(id, "cols") == 0) {
        struct winsize ws;
        if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
        return 80;
    }
    if (strcmp(id, "li") == 0 || strcmp(id, "lines") == 0) {
        struct winsize ws;
        if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return ws.ws_row;
        return 25;
    }
    if (strcmp(id, "colors") == 0 || strcmp(id, "Co") == 0) return 16;
    return -1;
}

char *tgetstr(const char *id, char **area) {
    if (!id) return NULL;
    const char *cap = NULL;
    if (strcmp(id, "cl") == 0) cap = "\033[2J\033[H";
    else if (strcmp(id, "cd") == 0) cap = "\033[J";
    else if (strcmp(id, "ce") == 0) cap = "\033[K";
    else if (strcmp(id, "cm") == 0) cap = "\033[%i%d;%dH";
    else if (strcmp(id, "up") == 0) cap = "\033[A";
    else if (strcmp(id, "do") == 0) cap = "\033[B";
    else if (strcmp(id, "le") == 0) cap = "\033[D";
    else if (strcmp(id, "nd") == 0) cap = "\033[C";
    else if (strcmp(id, "so") == 0) cap = "\033[7m";
    else if (strcmp(id, "se") == 0) cap = "\033[27m";
    else if (strcmp(id, "us") == 0) cap = "\033[4m";
    else if (strcmp(id, "ue") == 0) cap = "\033[24m";
    else if (strcmp(id, "md") == 0) cap = "\033[1m";
    else if (strcmp(id, "me") == 0) cap = "\033[0m";
    else if (strcmp(id, "AF") == 0) cap = "\033[3%dm";
    else if (strcmp(id, "AB") == 0) cap = "\033[4%dm";
    else if (strcmp(id, "bc") == 0) cap = "\b";
    else if (strcmp(id, "cr") == 0) cap = "\r";
    else if (strcmp(id, "nl") == 0) cap = "\n";
    else if (strcmp(id, "vi") == 0) cap = "\033[?25l";
    else if (strcmp(id, "ve") == 0) cap = "\033[?25h";

    if (!cap) return NULL;
    if (area && *area) {
        char *ret = *area;
        strcpy(ret, cap);
        *area += strlen(cap) + 1;
        return ret;
    }
    return (char *)cap;
}

char *tgoto(const char *cap, int col, int row) {
    static char buf[32];
    if (!cap) return "";
    snprintf(buf, sizeof(buf), "\033[%d;%dH", row + 1, col + 1);
    return buf;
}

int tputs(const char *cp, int affcnt, int (*outc)(int)) {
    (void)affcnt;
    if (!cp || !outc) return 0;
    while (*cp) {
        outc((unsigned char)*cp++);
    }
    return 0;
}

void __assert_rtn(const char *func, const char *file, int line, const char *failedexpr) {
    if (func && (uintptr_t)func != (uintptr_t)-1L) {
        fprintf(stderr, "Assertion failed: (%s), function %s, file %s, line %d.\n",
                failedexpr ? failedexpr : "", func, file ? file : "", line);
    } else {
        fprintf(stderr, "Assertion failed: (%s), file %s, line %d.\n",
                failedexpr ? failedexpr : "", file ? file : "", line);
    }
    abort();
}









