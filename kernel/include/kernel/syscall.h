// XIU System Call Interface
#pragma once
#ifndef CHIMERA_SYSCALL_H
#define CHIMERA_SYSCALL_H

#include <kernel/chimera_types.h>
#include <kernel/bsd_syscall_xnu.h>

#define MAX_SYSCALL_NUM 512

typedef i64 (*syscall_fn_t)(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6);

extern const syscall_fn_t g_syscall_table[];
extern const u32 g_syscall_count;

i64 syscall_dispatch(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6, u64 frame);

#endif
