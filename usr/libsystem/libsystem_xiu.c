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

#ifndef NULL
#define NULL ((void *)0)
#endif

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
extern i64 xiu_mkdir(const char *path, u32 mode);
extern i64 xiu_ioctl(int fd, u64 cmd, void *arg);
extern i64 xiu_fcntl(int fd, int cmd, u64 arg);
extern i64 xiu_lseek(int fd, i64 offset, int whence);
extern i64 xiu_getcwd(char *buf, usize size);
extern i64 xiu_pipe(int pipefd[2]);
extern i64 xiu_dup2(int oldfd, int newfd);
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

i64 write(int fd, const void *buf, usize len) {
  return xiu_write(fd, buf, len);
}

i64 read(int fd, void *buf, usize len) {
  return xiu_read(fd, buf, len);
}

int open(const char *path, int flags, int mode) {
  return (int)xiu_open(path, flags, mode);
}

int close(int fd) { return (int)xiu_close(fd); }

int stat(const char *path, struct stat *buf) {
  return (int)xiu_stat(path, buf);
}

int mkdir(const char *path, u32 mode) { return (int)xiu_mkdir(path, mode); }

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

DIR *opendir(const char *name) {
  int fd = open(name, 0, 0);
  if (fd < 0)
    return NULL;

  DIR *dir = (DIR *)malloc(sizeof(DIR));
  if (!dir) {
    close(fd);
    return NULL;
  }
  dir->fd = fd;
  return dir;
}

struct dirent *readdir(DIR *dirp) {
  if (xiu_getdents(dirp->fd, &dirp->entry, sizeof(struct dirent)) <= 0) {
    return NULL;
  }
  return &dirp->entry;
}

int closedir(DIR *dirp) {
  int fd = dirp->fd;
  free(dirp);
  return close(fd);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * §1  Mach IPC Library Wrappers
 * ═══════════════════════════════════════════════════════════════════════════
 */

typedef struct {
  u32 msgh_bits;
  u32 msgh_size;
  u32 msgh_remote_port;
  u32 msgh_local_port;
  u32 msgh_voucher_port;
  u32 msgh_id;
  u32 msgh_reserved1;
  u32 msgh_reserved2;
} xiu_msg_header_t;
typedef xiu_msg_header_t mach_msg_header_t;

typedef u32 ipc_space_t;
typedef u32 mach_port_right_t;
typedef u32 mach_port_name_t;

#define MACH_PORT_RIGHT_RECEIVE 1
#define MACH_PORT_NULL 0
#define MACH_PORT_NAME_NULL 0

extern i64 xiu_mach_port_allocate(ipc_space_t space, mach_port_right_t right,
                                  mach_port_name_t *name);
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
    return (pid_t)xiu_wait4(pid, status, options, NULL);
}

pid_t getpid(void) { return (pid_t)xiu_getpid(); }
pid_t getppid(void) { return 0; }
uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }
int tcsetpgrp(int fd, pid_t pgrp) { return 0; }
int dup(int oldfd) { return dup2(oldfd, oldfd); }
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
  return (int)xiu_unlink((u64)path);
}

int rmdir(const char *path) {
  if (!path) return -1;
  return (int)xiu_rmdir((u64)path);
}

int mkstemp(char *tmpl) { return -1; }
int access(const char *path, int mode) { return 0; }
int umask(int mask) { return 022; }

#include "include/termios.h"

int sigemptyset(sigset_t *set) { *set = 0; return 0; }
int sigfillset(sigset_t *set) { *set = 0xFFFFFFFF; return 0; }
int sigaddset(sigset_t *set, int signum) { *set |= (1 << (signum - 1)); return 0; }
int sigdelset(sigset_t *set, int signum) { *set &= ~(1 << (signum - 1)); return 0; }
int sigismember(const sigset_t *set, int signum) { return (*set & (1 << (signum - 1))) != 0; }

int tcgetattr(int fd, struct termios *termios_p) {
    if (!termios_p) return -1;
    return ioctl(fd, 0x5401, termios_p);
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
    (void)optional_actions;
    if (!termios_p) return -1;
    return ioctl(fd, 0x5402, (void *)termios_p);
}

// string Utilities

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
      if (!s) s = "(null)";
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

void *malloc(usize size) {
  if (size == 0) return NULL;
  
  // align to 16 bytes
  size = (size + 15) & ~15;

  struct malloc_chunk *curr = g_malloc_list;
  while (curr) {
    if (curr->free && curr->size >= size) {
      curr->free = 0;
      return (void *)(curr + 1);
    }
    curr = curr->next;
  }

  // allocate new pages
  usize alloc_size = size + sizeof(struct malloc_chunk);
  if (alloc_size < 65536) alloc_size = 65536;
  
  void *ptr = mmap(NULL, alloc_size, 3, 0x22, -1, 0);
  if (ptr == (void *)-1) return NULL;

  struct malloc_chunk *chunk = (struct malloc_chunk *)ptr;
  chunk->size = alloc_size - sizeof(struct malloc_chunk);
  chunk->free = 0;
  chunk->next = g_malloc_list;
  g_malloc_list = chunk;

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
  struct malloc_chunk *curr = g_malloc_list;
  while (curr) {
    if ((void *)(curr + 1) == ptr) {
      curr->free = 1;
      return;
    }
    curr = curr->next;
  }
}


int fstat(int fd, struct stat *buf) {
  return 0; // stub
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

i64 getline(char **lineptr, usize *n, FILE *stream) {
    if (!lineptr || !n) return -1;
    int fd = stream ? (((uptr)stream <= 2) ? (int)(uptr)stream : stream->fd) : 0;
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

int ftruncate(int fd, i64 length) {
    (void)fd; (void)length;
    return 0;
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
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : (f ? f->fd : 1);
  write(fd, buf, res);
  return res;
}

int vfprintf(FILE *f, const char *fmt, __builtin_va_list args) {
  char buf[2048];
  int res = vsnprintf(buf, sizeof(buf), fmt, args);
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : (f ? f->fd : 1);
  write(fd, buf, res);
  return res;
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
  f->fd = fd;
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
  f->fd = fd;
  return f;
}
int fclose(FILE *f) {
  if (!f) return -1;
  if ((uptr)f > 2) {
    close(f->fd);
    free(f);
  }
  return 0;
}

int mprotect(void *addr, usize len, int prot) {
  (void)addr; (void)len; (void)prot;
  return 0;
}

float strtof(const char *nptr, char **endptr) {
  return (float)strtod(nptr, endptr);
}

long double strtold(const char *nptr, char **endptr) {
  return (long double)strtod(nptr, endptr);
}

static struct tm s_tm_buf;
struct tm *localtime(const time_t *timer) {
  time_t t = timer ? *timer : 0;
  __builtin_memset(&s_tm_buf, 0, sizeof(s_tm_buf));
  s_tm_buf.tm_year = 126; // 2026 - 1900
  s_tm_buf.tm_mon = 7;   // august
  s_tm_buf.tm_mday = 19;
  s_tm_buf.tm_hour = 3;
  s_tm_buf.tm_min = 27;
  s_tm_buf.tm_sec = (int)(t % 60);
  return &s_tm_buf;
}

struct tm *gmtime(const time_t *timer) {
  return localtime(timer);
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
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : f->fd;
  i64 res = read(fd, ptr, size * n);
  if (res <= 0) return 0;
  return (usize)res / size;
}

usize fwrite(const void *ptr, usize size, usize n, FILE *f) {
  if (!f || !ptr || size == 0 || n == 0) return 0;
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : f->fd;
  i64 res = write(fd, ptr, size * n);
  if (res <= 0) return 0;
  return (usize)res / size;
}

int fputc(int c, FILE *f) {
  char ch = (char)c;
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : (f ? f->fd : 1);
  write(fd, &ch, 1);
  return c;
}

int fputs(const char *s, FILE *f) {
  if (!s) return 0;
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : (f ? f->fd : 1);
  return (int)write(fd, s, strlen(s));
}

int fseek(FILE *f, long offset, int whence) {
  if (!f) return -1;
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : f->fd;
  i64 res = lseek(fd, (i64)offset, whence);
  return (res >= 0) ? 0 : -1;
}

long ftell(FILE *f) {
  if (!f) return -1;
  int fd = ((uptr)f <= 2) ? (int)(uptr)f : f->fd;
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

int fflush(FILE *f) { return 0; }
int sscanf(const char *str, const char *fmt, ...) { return 0; }
int getrlimit(int res, struct rlimit *rl) { if (rl) { rl->rlim_cur = 0; rl->rlim_max = 0; } return -1; }
int setrlimit(int res, const struct rlimit *rl) { return -1; }

char *strerror(int errnum) {
  static char buf[32];
  sprintf(buf, "Error %d", errnum);
  return buf;
}

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

int gettimeofday(struct timeval *tv, void *tz) {
  (void)tz;
  if (tv) {
    tv->tv_sec = 1755567890;
    tv->tv_usec = 0;
  }
  return 0;
}

char *stpncpy(char *dest, const char *src, usize n) {
  usize i;
  for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
  char *res = dest + i;
  for (; i < n; i++) dest[i] = '\0';
  return res;
}

int killpg(pid_t pgrp, int sig) { return kill(-pgrp, sig); }
int isatty(int fd) { return fd < 3; }
int tcgetpgrp(int fd) { return 0; }
int setpgid(pid_t pid, pid_t pgid) { return 0; }
pid_t getpgrp(void) { return 0; }
char *setlocale(int category, const char *locale) { return "C"; }
int strcoll(const char *s1, const char *s2) { return strcmp(s1, s2); }
int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = *s1; if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        int c2 = *s2; if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return *s1 - *s2;
}

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

sighandler_t signal(int signum, sighandler_t handler) {
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

int dup2(int oldfd, int newfd) {
    return (int)xiu_dup2(oldfd, newfd);
}

const char *sys_siglist[NSIG] = {
    [0] = "Signal 0", [SIGHUP] = "Hangup", [SIGINT] = "Interrupt", [SIGQUIT] = "Quit",
    [SIGILL] = "Illegal instruction", [SIGTRAP] = "Trace/breakpoint trap", [SIGABRT] = "Aborted",
    [SIGFPE] = "Floating point exception", [SIGKILL] = "Killed", [SIGUSR1] = "User defined signal 1",
    [SIGSEGV] = "Segmentation fault", [SIGUSR2] = "User defined signal 2", [SIGPIPE] = "Broken pipe",
    [SIGALRM] = "Alarm clock", [SIGTERM] = "Terminated", [SIGCHLD] = "Child exited",
    [SIGCONT] = "Continued", [SIGSTOP] = "Stopped (signal)", [SIGTSTP] = "Stopped",
    [SIGTTIN] = "Stopped (tty input)", [SIGTTOU] = "Stopped (tty output)"
};

int fstat64(int fd, struct stat64 *buf) { return fstat(fd, (struct stat *)buf); }
intmax_t strtoimax(const char *nptr, char **endptr, int base) { return (intmax_t)strtoll(nptr, endptr, base); }
char *strsignal(int sig) {
    if (sig >= 0 && sig < NSIG && sys_siglist[sig]) return (char *)sys_siglist[sig];
    return "Unknown signal";
}

int getgroups(int size, gid_t list[]) { return 0; }
clock_t times(struct tms *buffer) { return 0; }

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

// wchar stubs
usize mbrlen(const char *s, usize n, mbstate_t *ps) { return 1; }
usize mbrtowc(wchar_t *pwc, const char *s, usize n, mbstate_t *ps) { if (pwc) *pwc = *s; return 1; }
usize mbsrtowcs(wchar_t *dest, const char **src, usize len, mbstate_t *ps) { return 0; }
wchar_t *wcschr(const wchar_t *s, wchar_t c) { return NULL; }

// generic syscall wrapper for variadic syscalls
extern i64 xiu_syscall(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6);

long syscall(long number, ...) {
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

// darwin mach ipc apis

extern i64 xiu_mach_port_deallocate(u64 space, u64 name);
extern i64 xiu_mach_port_type(u64 space, u64 name, u64 ptype_out);
extern i64 xiu_task_self(void);

mach_port_name_t mach_task_self(void) {
    return (mach_port_name_t)xiu_task_self();
}

int mach_msg_trap(void *msg, int option, unsigned int send_size, unsigned int rcv_size,
                  unsigned int rcv_name, unsigned int timeout, unsigned int notify) {
    (void)notify;
    return (int)xiu_mach_msg(msg, (u32)option, (u32)send_size, (u32)rcv_size,
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

    int rc = mach_msg_trap(&req, 1, sizeof(req), 0, 0, 1000, 0);
    if (rc != 0) {
        mach_port_deallocate(mach_task_self(), reply_port);
        return rc;
    }

    rep_t rep;
    __builtin_memset(&rep, 0, sizeof(rep));
    rc = mach_msg_trap(&rep, 2, 0, sizeof(rep) + 64, reply_port, 1000, 0);
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

    int rc = mach_msg_trap(&req, 1, sizeof(req), 0, 0, 1000, 0);
    if (rc != 0) {
        mach_port_deallocate(mach_task_self(), reply_port);
        return rc;
    }

    rep_t rep;
    __builtin_memset(&rep, 0, sizeof(rep));
    rc = mach_msg_trap(&rep, 2, 0, sizeof(rep) + 64, reply_port, 1000, 0);
    mach_port_deallocate(mach_task_self(), reply_port);
    if (rc != 0) return rc;
    return (int)rep.ret_code;
}

// darwin mach bootstrap server apis

#include <bootstrap.h>

mach_port_t bootstrap_port = 1; // default root bootstrap port

extern i64 xiu_mach_register_service(const char *name, mach_port_name_t port_name);
extern i64 xiu_mach_lookup_service(const char *name, mach_port_name_t *port_out);

kern_return_t bootstrap_register(mach_port_t bp, const char *service_name, mach_port_t sp) {
    (void)bp;
    if (!service_name) return BOOTSTRAP_UNKNOWN_SERVICE;
    return (xiu_mach_register_service(service_name, (mach_port_name_t)sp) == 0) ? BOOTSTRAP_SUCCESS : BOOTSTRAP_NO_MEMORY;
}

kern_return_t bootstrap_look_up(mach_port_t bp, const char *service_name, mach_port_t *sp) {
    (void)bp;
    if (!service_name || !sp) return BOOTSTRAP_UNKNOWN_SERVICE;
    mach_port_name_t p = 0;
    if (xiu_mach_lookup_service(service_name, &p) != 0 || p == 0) {
        return BOOTSTRAP_UNKNOWN_SERVICE;
    }
    *sp = (mach_port_t)p;
    return BOOTSTRAP_SUCCESS;
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

int rename(const char *oldpath, const char *newpath) {
    (void)oldpath;
    (void)newpath;
    return -1;
}

int chmod(const char *path, mode_t mode) {
    (void)path;
    (void)mode;
    return 0;
}

int getpagesize(void) {
    return 4096;
}



