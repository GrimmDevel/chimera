// x86_64 lapic driver header
#pragma once
#ifndef XIU_LAPIC_H
#define XIU_LAPIC_H

#include <kernel/xiu_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAPIC_ID                0x0020
#define LAPIC_VERSION           0x0030
#define LAPIC_TPR               0x0080
#define LAPIC_APR               0x0090
#define LAPIC_PPR               0x00A0
#define LAPIC_EOI               0x00B0
#define LAPIC_RRD               0x00C0
#define LAPIC_LDR               0x00D0
#define LAPIC_DFR               0x00E0
#define LAPIC_SVR               0x00F0
#define LAPIC_ISR_BASE          0x0100
#define LAPIC_TMR_BASE          0x0180
#define LAPIC_IRR_BASE          0x0200
#define LAPIC_ESR               0x0280
#define LAPIC_ICR_LOW           0x0300
#define LAPIC_ICR_HIGH          0x0310
#define LAPIC_LVT_TIMER         0x0320
#define LAPIC_LVT_THERMAL       0x0330
#define LAPIC_LVT_PERF          0x0340
#define LAPIC_LVT_LINT0         0x0350
#define LAPIC_LVT_LINT1         0x0360
#define LAPIC_LVT_ERROR         0x0370
#define LAPIC_TIMER_INIT_CNT    0x0380
#define LAPIC_TIMER_CURR_CNT    0x0390
#define LAPIC_TIMER_DIV_CFG     0x03E0

#define ICR_FIXED               (0x0 << 8)
#define ICR_LOWEST              (0x1 << 8)
#define ICR_SMI                 (0x2 << 8)
#define ICR_NMI                 (0x4 << 8)
#define ICR_INIT                (0x5 << 8)
#define ICR_STARTUP             (0x6 << 8)

#define ICR_DEST_NONE           (0x0 << 18)
#define ICR_DEST_SELF           (0x1 << 18)
#define ICR_DEST_ALL_INC_SELF   (0x2 << 18)
#define ICR_DEST_ALL_EXC_SELF   (0x3 << 18)

#define ICR_ASSERT              (0x1 << 14)
#define ICR_DEASSERT            (0x0 << 14)
#define ICR_LEVEL_TRIGGER       (0x1 << 15)
#define ICR_EDGE_TRIGGER        (0x0 << 15)
#define ICR_BUSY                (0x1 << 12)

#define VECTOR_IPI_SCHED        0xEE
#define VECTOR_IPI_TLB          0xEF
#define VECTOR_IPI_PANIC        0xFE
#define VECTOR_SPURIOUS         0xFF

void lapic_init_bsp(void);
void lapic_init_ap(void);
void lapic_eoi(void);
u32  lapic_get_id(void);
void lapic_send_ipi(u32 lapic_id, u8 vector);
void lapic_send_ipi_all_excluding_self(u8 vector);
void lapic_timer_init(u32 ticks);

#ifdef __cplusplus
}
#endif

#endif
