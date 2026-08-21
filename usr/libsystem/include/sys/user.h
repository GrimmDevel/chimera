/*
 * Copyright (c) 2000-2018 Apple Inc. All rights reserved.
 * APPLE_OSREFERENCE_LICENSE
 *
 * FreeBSD-compat kinfo_proc for userspace (kvm consumers).
 * ponytail: ravynOS WindowServer uses ki_pid/ki_uid/ki_rgid from FreeBSD kinfo_proc.
 * xiu doesn't have kvm — these are stub definitions to satisfy compilation.
 */

#ifndef _SYS_USER_H_
#define _SYS_USER_H_

#include <sys/types.h>
#include <sys/time.h>

/* freebsd-compat kinfo_proc with the fields WindowServer actually touches */
struct kinfo_proc {
    int      ki_structsize;
    pid_t    ki_pid;
    pid_t    ki_ppid;
    pid_t    ki_pgid;
    uid_t    ki_uid;
    uid_t    ki_ruid;
    gid_t    ki_rgid;
    gid_t    ki_svgid;
    int      ki_stat;
    char     ki_comm[20];
};

struct user {
    /* not used */
};

#endif /* !_SYS_USER_H_ */
