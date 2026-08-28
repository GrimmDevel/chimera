/* =============================================================================
 * Chimera Operating System — Global Descriptor Table (x86_64)
 * kernel/arch/x86_64/gdt.h
 * ============================================================================= */

#pragma once
#ifndef CHIMERA_GDT_H
#define CHIMERA_GDT_H

#include <kernel/chimera_types.h>

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18
#define GDT_USER_CODE   0x20
#define GDT_TSS         0x28

struct tss_entry {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iopb_offset;
} CHIMERA_PACKED;

void gdt_init(void);
void gdt_init_ap(u64 *ap_gdt, struct tss_entry *ap_tss, u32 cpu_id);
void tss_set_rsp0(u64 rsp0);
void tss_set_rsp0_cpu(u32 cpu_id, u64 rsp0);

#endif /* CHIMERA_GDT_H */
