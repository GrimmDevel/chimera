// ACPI MADT (Multiple APIC Description Table) parser
// kernel/arch/x86_64/madt.h
#pragma once
#ifndef CHIMERA_MADT_H
#define CHIMERA_MADT_H

#include <kernel/chimera_types.h>

#define MADT_MAX_LAPICS 16
#define MADT_MAX_IOAPICS 4

typedef struct {
    u8  lapic_ids[MADT_MAX_LAPICS];  // APIC IDs of all CPUs (BSP first)
    u8  lapic_bsp_id;                // BSP's APIC ID
    u32 lapic_count;                 // number of CPUs found
    u64 ioapic_base[MADT_MAX_IOAPICS];
    u32 ioapic_gsi_base[MADT_MAX_IOAPICS];
    u32 ioapic_count;
    bool valid;                      // MADT was found and parsed
} madt_info_t;

// Parse the ACPI MADT starting from the RSDP. Returns false if ACPI/MADT
// cannot be found (the caller should fall back to 1-CPU assumptions).
bool madt_parse(u64 rsdp_phys, madt_info_t *out);

#endif // CHIMERA_MADT_H
