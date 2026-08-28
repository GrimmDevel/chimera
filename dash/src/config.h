/* =============================================================================
 * Chimera Operating System — dash config.h
 * dash/src/config.h
 * ============================================================================= */

#ifndef CHIMERA_DASH_CONFIG_H
#define CHIMERA_DASH_CONFIG_H

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1
#define HAVE_UNISTD_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STDIO_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_SETJMP_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_PATHS_H 1

/* Memory */
#define HAVE_MALLOC 1
#define HAVE_FREE 1
#define HAVE_REALLOC 1

/* Tell dash we provide killpg to prevent its static inline definition */
#define HAVE_KILLPG 1

/* Tell dash system.h to skip its killpg stub */
#undef HAVE_SIGSETMASK

/* Tell dash we have strtoimax as strtoll */
#define HAVE_STRTOIMAX 1

/* Suppress things we stub/don't have */
#define HAVE_GETRLIMIT 1
#undef HAVE_SYS_SIGLIST
#undef HAVE_STAT64
#undef HAVE_FPATHCONF
#undef HAVE_MEMPCPY

/* We provide these */
#define HAVE_STPCPY 1
#define HAVE_GETCWD 1
#define HAVE_MEMSET 1
#define HAVE_STRCHR 1
#define HAVE_STRSIGNAL 1
#define HAVE_ISALPHA 1
#define HAVE_ISBLANK 1
#define HAVE_DECL_ISBLANK 1
#define HAVE_SYSCONF 1

/* Version information */
#define PACKAGE "dash"
#define PACKAGE_VERSION "0.5.11"

/* Support for standard XIU environment */
#define DEFAULT_PATH "/bin:/usr/bin"
#define DEFAULT_TERM "xiu-terminal"

#undef BSD
#define BSD 1
#define HAVE_ALLOCA 1
#define USE_MEMFD_CREATE 0
#define HAVE_MEMFD_CREATE 0
#define HAVE_F_DUPFD_CLOEXEC 0
#define memfd_create(name, flags) (-1)
#include <sys/stat.h>
#define open64 open
#define stat64 stat
#define fstat64 fstat
#define lstat64 lstat

#endif
