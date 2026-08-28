/* =============================================================================
 * Chimera Operating System — Symmetric Multiprocessing (SMP) Subsystem
 * kernel/include/kernel/smp.h
 *
 * Apple XNU-style multi-core CPU discovery, AP boot sequence, and topology
 * ============================================================================= */

#pragma once
#ifndef CHIMERA_SMP_H
#define CHIMERA_SMP_H

#include <kernel/chimera_types.h>
#include <kernel/proc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHIMERA_MAX_CPUS 16

// core SMP Functions
void smp_init(void);
u32  smp_get_cpu_count(void);
u32  smp_get_active_cpus(void);
u32  smp_current_cpu_id(void);

// cross-core IPI functions
void smp_send_reschedule(u32 cpu_id);
void smp_broadcast_reschedule(void);
void smp_tlb_shootdown(void);
void smp_tlb_flush_range(u64 start_va, usize size);
void smp_tlb_flush_page(u64 va);

#ifdef __cplusplus
}
#endif

#endif /* CHIMERA_SMP_H */
