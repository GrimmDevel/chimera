/*
 * zsh config.h for XIU OS (Darwin Mach-O 64-bit)
 */
#ifndef ZSH_CONFIG_H
#define ZSH_CONFIG_H

#define PROTOTYPES 1
#define STDC_HEADERS 1
#define HAVE_STDARG_H 1
#define HAVE_CONFIG_H 1
#define ZSH_STATIC 1
#define MACH 1
#define __MACH__ 1
#define __XIU__ 1
#define __APPLE__ 1

#define mod_export
#define mod_import_variable
#define mod_import_function

#define PASSWD_FILE "/etc/passwd"
#define PASSWD_MAP "passwd.byname"
#define GLOBAL_ZSHRC "/etc/zshrc"
#define GLOBAL_ZPROFILE "/etc/zprofile"
#define GLOBAL_ZSHENV "/etc/zshenv"
#define GLOBAL_ZLOGIN "/etc/zlogin"
#define CACHE_USERNAMES 1
#define JOB_CONTROL 1
#define USE_SUSPENDED 1
#define DEFAULT_HISTSIZE 100
#define DEFAULT_FCEDIT "vi"
#define DEFAULT_TMPPREFIX "/tmp/zsh"
#define DEFAULT_PATH "/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin"
#define SIGCOUNT 31

#define MACHTYPE "x86_64"
#define VENDOR "apple"
#define OSTYPE "xiu"
#define DEFAULT_READNULLCMD "cat"
#define TGETENT_SUCCESS 1
#define HAVE_UNION_INIT 1

#define ZSH_64_BIT_TYPE long
#define ZSH_64_BIT_UTYPE unsigned long
#define ZLONG_IS_LONG_64 1

#define HAVE_ALLOCA 1
#define HAVE_ALLOCA_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STRINGS_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_DIRENT_H 1
#define HAVE_TERMIOS_H 1
#define HAVE_SYS_TERMIOS_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TIMES_H 1
#define TIME_WITH_SYS_TIME 1
#define HAVE_STRUCT_TIMEZONE 1
#define HAVE_STRUCT_TIMESPEC 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_SYS_RESOURCE_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_UTSNAME_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_POLL_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_SYS_SIGNAL_H 1
#define HAVE_LOCALE_H 1
#define HAVE_SETJMP_H 1
#define HAVE_GRP_H 1
#define HAVE_PWD_H 1
#define HAVE_ERRNO_H 1

#define HAVE_ISINF 1
#define HAVE_ISNAN 1
#define HAVE_FINITE 1
#define HAVE_GETRUSAGE 1
#define HAVE_GETRLIMIT 1
#define HAVE_SETRLIMIT 1
#define HAVE_GETHOSTNAME 1

#define HAVE_SETUID 1
#define HAVE_SETEUID 1
#define HAVE_SETREUID 1
#define HAVE_SETGID 1
#define HAVE_SETEGID 1
#define HAVE_SETREGID 1
#define HAVE_SETRESUID 1
#define HAVE_SETRESGID 1
#define HAVE_SETPGID 1

#define HAVE_KILLPG 1
#define HAVE_TCSETPGRP 1
#define HAVE_TCGETPGRP 1
#define HAVE_ISATTY 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_SELECT 1
#define HAVE_POLL 1
#define HAVE_SETLOCALE 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STRSTR 1
#define HAVE_STRTOUL 1
#define HAVE_STRTOL 1
#define HAVE_STRTOD 1
#define HAVE_BSEARCH 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMCPY 1
#define HAVE_MEMSET 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#define HAVE_GETCWD 1
#define HAVE_CHDIR 1
#define HAVE_GETPID 1
#define HAVE_GETPPID 1
#define HAVE_GETUID 1
#define HAVE_GETEUID 1
#define HAVE_GETGID 1
#define HAVE_GETEGID 1
#define HAVE_GETPWUID 1
#define USE_GETPWUID 1
#define HAVE_GETPWNAM 1
#define HAVE_GETGRGID 1
#define HAVE_GETGRNAM 1
#define HAVE_FORK 1
#define HAVE_VFORK 1
#define HAVE_EXECVE 1
#define HAVE_WAITPID 1
#define HAVE_PIPE 1
#define HAVE_DUP2 1
#define HAVE_ACCESS 1
#define HAVE_LSTAT 1
#define HAVE_STAT 1
#define HAVE_FSTAT 1
#define HAVE_OPEN 1
#define HAVE_CLOSE 1
#define HAVE_READ 1
#define HAVE_WRITE 1
#define HAVE_LSEEK 1
#define HAVE_UNLINK 1
#define HAVE_RMDIR 1
#define HAVE_MKDIR 1
#define HAVE_LINK 1
#define HAVE_RENAME 1
#define HAVE_READLINK 1
#define HAVE_SYMLINK 1
#define HAVE_IOCTL 1
#define HAVE_TCGETATTR 1
#define HAVE_TCSETATTR 1
#define HAVE_SIGACTION 1
#define HAVE_SIGPROCMASK 1
#define HAVE_SIGEMPTYSET 1
#define HAVE_SIGADDSET 1
#define HAVE_SIGDELSET 1
#define HAVE_SIGISMEMBER 1
#define HAVE_SIGFILLSET 1
#define POSIX_SIGNALS 1

#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_LONG_LONG 8
#define SIZEOF_OFF_T 8
#define SIZEOF_INO_T 8
#define SIZEOF_TIME_T 8
#define SIZEOF_VOID_P 8

#define HAVE_STRUCT_STAT_ST_ATIMESPEC 1
#define HAVE_STRUCT_STAT_ST_MTIMESPEC 1
#define HAVE_STRUCT_STAT_ST_CTIMESPEC 1
#define HAVE_STRUCT_STAT_ST_BLOCKS 1
#define HAVE_STRUCT_STAT_ST_BLKSIZE 1
#define HAVE_STRUCT_STAT_ST_RDEV 1
#define HAVE_STRUCT_STAT_ST_FLAGS 1

#define USE_GETCWD 1
#define GETPGRP_VOID 1

#define HAVE_WCHAR_H 1
#define HAVE_WCTYPE_H 1
#define HAVE_WCRTOMB 1
#define HAVE_MBRLEN 1
#define HAVE_MBRTOWC 1
#define HAVE_WCWIDTH 1

#endif /* ZSH_CONFIG_H */
