#ifndef SUDO_CONFIG_H
#define SUDO_CONFIG_H

#define PACKAGE_NAME "sudo"
#define PACKAGE_VERSION "1.9.12p1"
#define PACKAGE_STRING "sudo 1.9.12p1"
#define PACKAGE_BUGREPORT "https://github.com/GrimmDevel/xiu"

#define STDC_HEADERS 1
#define HAVE_STDARG_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_TERMIOS_H 1
#define HAVE_PWD_H 1
#define HAVE_GRP_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_ERRNO_H 1
#define HAVE_PATHS_H 1
#define HAVE_NET_IF_H 1
#define HAVE_SYS_SOCKIO_H 1
#define HAVE_STDBOOL_H 1
#define HAVE__BOOL 1
#define HAVE___FUNC__ 1
#define HAVE_DECL___FUNC__ 1

#define HAVE_FORK 1
#define HAVE_VFORK 1
#define HAVE_EXECVE 1
#define HAVE_WAITPID 1
#define HAVE_PIPE 1
#define HAVE_DUP2 1
#define HAVE_SETUID 1
#define HAVE_SETEUID 1
#define HAVE_SETGID 1
#define HAVE_SETEGID 1
#define HAVE_GETUID 1
#define HAVE_GETEUID 1
#define HAVE_GETGID 1
#define HAVE_GETEGID 1
#define HAVE_GETGROUPLIST 1
#define HAVE_GETPWUID 1
#define HAVE_GETPWNAM 1
#define HAVE_GETGRGID 1
#define HAVE_GETGRNAM 1
#define HAVE_GETHOSTNAME 1
#define HAVE_ISATTY 1
#define HAVE_TTYNAME 1
#define HAVE_TCGETATTR 1
#define HAVE_TCSETATTR 1
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#define HAVE_STRDUP 1
#define HAVE_STRNDUP 1
#define HAVE_STRLCPY 1
#define HAVE_STRLCAT 1
#define HAVE_SNPRINTF 1
#define HAVE_VSNPRINTF 1
#define HAVE_ASPRINTF 1
#define HAVE_VASPRINTF 1
#define HAVE_GETOPT 1
#define HAVE_UTIMES 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_GMTIME_R 1
#define HAVE_LOCALTIME_R 1
#define HAVE_NANOSLEEP 1
#define HAVE_KILLPG 1
#define HAVE_SIGACTION 1
#define HAVE_SIGPROCMASK 1
#define HAVE_TCGETPGRP 1
#define HAVE_TCSETPGRP 1
#define HAVE_TERMIOS 1
#define HAVE_STRUCT_DIRENT_D_TYPE 1
#define HAVE_STRUCT_STAT_ST_MTIM 1
#define HAVE_STRUCT_STAT_ST_MTIMESPEC 1
#define HAVE_FCHMODAT 1
#define HAVE_FSTATAT 1
#define HAVE_FUTIMENS 1
#define HAVE_UTIMENSAT 1
#define HAVE_CLOSEFROM 1
#define HAVE_EXPLICIT_BZERO 1
#define HAVE_FREEZERO 1
#define HAVE_GETDELIM 1
#define HAVE_GETLINE 1
#define HAVE_GETUSERSHELL 1
#define HAVE_GETENTROPY 1
#define HAVE_GETPROGNAME 1
#define HAVE_SETPROGNAME 1
#define HAVE_PROGNAME 1
#define HAVE_TIMEGM 1
#define HAVE_ISBLANK 1
#define HAVE_OPTRESET 1
#define getenv_unhooked(x) getenv(x)

#define GETGROUPS_T gid_t
#define CONFIGURE_ARGS ""
#define _PATH_SUDO_BSHELL "/bin/sh"
#define _PATH_BSHELL "/bin/sh"
#define _PATH_VI "/bin/vi"
#define _PATH_SUDO_CONF "/etc/sudo.conf"
#define _PATH_SUDOERS "/etc/sudoers"
#define _PATH_SUDO_TIMEDIR "/var/db/sudo"

#define SUDO_STATIC 1
#define __MACH__ 1
#define __XIU__ 1
#define __APPLE__ 1

#ifndef sudo_dso_public
# define sudo_dso_public __attribute__((__visibility__("default")))
#endif
#ifndef sudo_dso_private
# define sudo_dso_private __attribute__((__visibility__("hidden")))
#endif
#ifndef sudo_dso_hidden
# define sudo_dso_hidden __attribute__((__visibility__("hidden")))
#endif
#ifndef sudo_dso_protected
# define sudo_dso_protected __attribute__((__visibility__("protected")))
#endif
#ifndef sudo_unused
# define sudo_unused __attribute__((__unused__))
#endif
#ifndef sudo_noreturn
# define sudo_noreturn __attribute__((__noreturn__))
#endif
#ifndef sudo_malloclike
# define sudo_malloclike __attribute__((__malloc__))
#endif
#ifndef sudo_printf0like
# define sudo_printf0like(a, b) __attribute__((__format__(__printf__, a, b)))
#endif
#ifndef sudo_printflike
# define sudo_printflike(a, b) __attribute__((__format__(__printf__, a, b)))
#endif
#ifndef sudo_attr_fmt_arg
# define sudo_attr_fmt_arg(x) __attribute__((__format_arg__(x)))
#endif
#ifndef sudo_attr_returns_nonnull
# define sudo_attr_returns_nonnull __attribute__((__returns_nonnull__))
#endif
#ifndef sudo_attr_cold
# define sudo_attr_cold __attribute__((__cold__))
#endif
#ifndef sudo_attr_warn_unused_result
# define sudo_attr_warn_unused_result __attribute__((__warn_unused_result__))
#endif
#ifndef FALLTHROUGH
# define FALLTHROUGH __attribute__((__fallthrough__))
#endif
#ifndef PUTENV_CONST
# define PUTENV_CONST
#endif
#ifndef IOCTL_REQ_CAST
# define IOCTL_REQ_CAST (unsigned long)
#endif
#ifndef __GNUC_PREREQ__
# if defined(__GNUC__) && defined(__GNUC_MINOR__)
#  define __GNUC_PREREQ__(maj, min) ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
# else
#  define __GNUC_PREREQ__(maj, min) 0
# endif
#endif

#endif /* SUDO_CONFIG_H */
