/* =============================================================================
 * XIU Operating System — Apple XNU Process Information Subsystem
 * kernel/include/kernel/proc_info_xnu.h
 * Derived from XNU bsd/sys/proc_info.h
 * ============================================================================= */

#ifndef XIU_PROC_INFO_XNU_H
#define XIU_PROC_INFO_XNU_H

#include <kernel/xiu_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROC_ALL_PIDS           1
#define PROC_PGRP_ONLY          2
#define PROC_PIDINFO            3
#define PROC_PIDTASKINFO        4

struct proc_bsdinfo {
    u32 pbi_flags;
    u32 pbi_status;
    u32 pbi_xstatus;
    u32 pbi_pid;
    u32 pbi_ppid;
    u32 pbi_uid;
    u32 pbi_gid;
    u32 pbi_ruid;
    u32 pbi_rgid;
    u32 pbi_svuid;
    u32 pbi_svgid;
    char pbi_name[32];
    char pbi_comm[32];
};

struct proc_taskinfo {
    u64 pti_virtual_size;
    u64 pti_resident_size;
    u64 pti_total_user;
    u64 pti_total_system;
    u32 pti_threads_count;
    u32 pti_num_faults;
};

#ifdef __cplusplus
}
#endif

#endif /* XIU_PROC_INFO_XNU_H */
