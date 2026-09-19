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

typedef enum smp_cpu_state {
    SMP_CPU_OFFLINE = 0,
    SMP_CPU_STARTING,
    SMP_CPU_ONLINE_IDLE,
    SMP_CPU_SCHEDULABLE,
    SMP_CPU_FAILED,
} smp_cpu_state_t;

typedef struct irq_stats {
    u64 count_pit;         // vector 32
    u64 count_lapic_timer; // vector 0xE0
    u64 count_ipi_sched;   // vector 0xEE
    u64 count_ipi_tlb;     // vector 0xEF
    u64 count_e1000_msi;   // vector 0x50
    u64 count_spurious;    // vector 0xFF
} irq_stats_t;

// core SMP Functions
void smp_init(void);
u32  smp_get_cpu_count(void);
u32  smp_get_active_cpus(void);
u32  smp_current_cpu_id(void);
smp_cpu_state_t smp_get_cpu_state(u32 cpu_id);
void smp_get_irq_stats(irq_stats_t *out);
void smp_dump_irq_stats(void);
u64  timer_get_monotonic_ticks(void);
void smp_delay_us(u32 us);

// cross-core IPI functions
void smp_send_reschedule(u32 cpu_id);
void smp_tlb_shootdown(void);
void smp_tlb_flush_range(u64 start_va, usize size);
void smp_tlb_flush_page(u64 va);
void smp_tlb_shootdown_pml4(u64 pml4_phys);
void smp_tlb_flush_range_pml4(u64 pml4_phys, u64 start_va, usize size);
void smp_tlb_flush_page_pml4(u64 pml4_phys, u64 va);

#ifdef __cplusplus
}
#endif

extern u32 g_active_cpus;
extern volatile bool g_smp_ready;

#endif /* CHIMERA_SMP_H */
