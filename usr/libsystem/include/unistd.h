#ifndef XIU_UNISTD_H
#define XIU_UNISTD_H

#include <kernel/xiu_types.h>
#include <sys/types.h>

i64 read(int fd, void *buf, usize count);
i64 write(int fd, const void *buf, usize count);
int close(int fd);
int chdir(const char *path);
char *getcwd(char *buf, usize size);
pid_t fork(void);
pid_t vfork(void);
pid_t waitpid(pid_t pid, int *status, int options);
int execve(const char *path, char *const argv[], char *const envp[]);
int execvp(const char *file, char *const argv[]);
extern char **environ;

// process
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setpgid(pid_t pid, pid_t pgid);
pid_t getpgrp(void);
int isatty(int fd);
int tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t pgrp);

// fd operations
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);
int getgroups(int size, gid_t list[]);
i64 lseek(int fd, i64 offset, int whence);
int fcntl(int fd, int cmd, ...);
int unlink(const char *path);
int ftruncate(int fd, i64 length);
int mkstemp(char *tmpl);

// access
int access(const char *path, int mode);
int umask(int mask);

// seek constants
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// access mode constants
#define X_OK 1
#define R_OK 4
#define W_OK 2
#define F_OK 0

// standard file descriptors
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// standard paths
#define _PATH_BSHELL "/bin/sh"
#define _PATH_TTY    "/dev/tty"
#define _PATH_DEVNULL "/dev/null"
#define _PATH_TMP    "/tmp/"

// standard functions
int usleep(unsigned int usec);
unsigned int sleep(unsigned int seconds);
int sched_yield(void);

// xiu Specific
void xiu_libc_init_terminal(u32 port, u64 win_id);

#endif
