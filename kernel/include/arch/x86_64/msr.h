/* =============================================================================
 * XIU Operating System — x86_64 Model Specific Registers (MSRs)
 * kernel/include/arch/x86_64/msr.h
 * ============================================================================= */

#pragma once

#include <kernel/xiu_types.h>

#define MSR_EFER        0xC0000080
#define MSR_STAR        0xC0000081
#define MSR_LSTAR       0xC0000082
#define MSR_CSTAR       0xC0000083
#define MSR_FMASK       0xC0000084
#define MSR_FS_BASE     0xC0000100
#define MSR_GS_BASE     0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

static inline void wrmsr(u32 msr, u64 value) {
    u32 low = (u32)value;
    u32 high = (u32)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

static inline u64 rdmsr(u32 msr) {
    u32 low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr) : "memory");
    return ((u64)high << 32) | low;
}
