// ACPI MADT (Multiple APIC Description Table) parser
// kernel/arch/x86_64/madt.c
//
// Walks RSDP → XSDT/RSDT → MADT ("APIC") and extracts the LAPIC IDs of all
// CPUs and the IOAPIC MMIO bases. Uses the HHDM to access ACPI tables.

#include "madt.h"
#include <kernel/chimera_types.h>

extern void kprintf(const char *fmt, ...);
extern u64 g_hhdm_base;

#define ACPI_RSDP_SIG 0x2052545020445352ULL // "RSD PTR "

// ACPI table header (common to all tables)
typedef struct {
    char     signature[4];
    u32      length;
    u8       revision;
    u8       checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    u32      oem_revision;
    u32      creator_id;
    u32      creator_revision;
} acpi_header_t;

// MADT entry types
#define MADT_TYPE_LAPIC   0
#define MADT_TYPE_IOAPIC  1

typedef struct {
    u32 lapic_addr;               // LAPIC MMIO base (typically 0xFEE00000)
    u32 flags;                    // bit 0: PCAT compat
} madt_body_t;

// Local APIC entry (type 0)
typedef struct {
    u8 type;       // 0
    u8 length;     // 8
    u8 acpi_uid;
    u8 apic_id;
    u32 flags;     // bit 0: enabled
} madt_lapic_t;

// IOAPIC entry (type 1)
typedef struct {
    u8 type;       // 1
    u8 length;     // 12
    u8 ioapic_id;
    u8 reserved;
    u32 mmio_addr;
    u32 gsi_base;
} madt_ioapic_t;

static inline u32 madt_read32(u64 phys) {
    return *(volatile u32 *)(phys + g_hhdm_base);
}

// Find the RSDT/XSDT from the RSDP and return the MADT's physical address.
static u64 madt_find_table(u64 rsdp_phys) {
    if (!rsdp_phys) return 0;

    // RSDP: "RSD PTR " (8 bytes), checksum(1), oemid(6), revision(1),
    //        rsdt_addr(4), length(4), xsdt_addr(8) ...
    u32 sig_lo = madt_read32(rsdp_phys);
    u32 sig_hi = *(volatile u32 *)(rsdp_phys + g_hhdm_base + 4);
    if (sig_lo != 0x20525450u || sig_hi != 0x20443552u) {
        // "RSD P" "TR " little-endian check
        volatile u8 *p = (volatile u8 *)(rsdp_phys + g_hhdm_base);
        if (p[0] != 'R' || p[1] != 'S' || p[2] != 'D' || p[3] != ' ' ||
            p[4] != 'P' || p[5] != 'T' || p[6] != 'R' || p[7] != ' ')
            return 0;
    }

    u8 revision = *(volatile u8 *)(rsdp_phys + g_hhdm_base + 15);
    u64 xsdt_addr = *(volatile u64 *)(rsdp_phys + g_hhdm_base + 24);
    u32 rsdt_addr = madt_read32(rsdp_phys + 16);

    // Try XSDT first (ACPI 2.0+), then RSDT (ACPI 1.0)
    if (revision >= 2 && xsdt_addr) {
        // XSDT: header + array of 64-bit table pointers
        u32 xsdt_len = *(volatile u32 *)(xsdt_addr + g_hhdm_base + 4);
        u32 entries = (xsdt_len - 36) / 8;
        for (u32 i = 0; i < entries; i++) {
            u64 tbl = *(volatile u64 *)(xsdt_addr + g_hhdm_base + 36 + i * 8);
            if (!tbl) continue;
            volatile acpi_header_t *h =
                (volatile acpi_header_t *)(tbl + g_hhdm_base);
            if (h->signature[0] == 'A' && h->signature[1] == 'P' &&
                h->signature[2] == 'I' && h->signature[3] == 'C')
                return tbl;
        }
    }

    if (rsdt_addr) {
        u32 rsdt_len = *(volatile u32 *)(rsdt_addr + g_hhdm_base + 4);
        u32 entries = (rsdt_len - 36) / 4;
        for (u32 i = 0; i < entries; i++) {
            u32 tbl = *(volatile u32 *)(rsdt_addr + g_hhdm_base + 36 + i * 4);
            if (!tbl) continue;
            volatile acpi_header_t *h =
                (volatile acpi_header_t *)(tbl + g_hhdm_base);
            if (h->signature[0] == 'A' && h->signature[1] == 'P' &&
                h->signature[2] == 'I' && h->signature[3] == 'C')
                return tbl;
        }
    }
    return 0;
}

bool madt_parse(u64 rsdp_phys, madt_info_t *out) {
    __builtin_memset(out, 0, sizeof(*out));

    u64 madt_phys = madt_find_table(rsdp_phys);
    if (!madt_phys) {
        kprintf("[MADT] ACPI table not found from RSDP 0x%llx\n",
                (unsigned long long)rsdp_phys);
        return false;
    }

    volatile acpi_header_t *hdr = (volatile acpi_header_t *)(madt_phys + g_hhdm_base);
    u32 madt_len = hdr->length;
    volatile madt_body_t *body =
        (volatile madt_body_t *)(madt_phys + g_hhdm_base + sizeof(acpi_header_t));

    // LAPIC MMIO base from the MADT body
    u64 lapic_mmio = body->lapic_addr;

    // Entries start right after the 8-byte MADT body
    u64 entry_base = madt_phys + g_hhdm_base + sizeof(acpi_header_t) + sizeof(madt_body_t);
    u64 entry_end = madt_phys + g_hhdm_base + madt_len;

    out->lapic_bsp_id = 0xFF; // set from the first (BSP) entry or LAPIC read

    for (u64 p = entry_base; p + 2 <= entry_end;) {
        u8 type = *(volatile u8 *)p;
        u8 len = *(volatile u8 *)(p + 1);
        if (len < 2 || p + len > entry_end) break;

        if (type == MADT_TYPE_LAPIC && len >= 8) {
            volatile madt_lapic_t *e = (volatile madt_lapic_t *)p;
            if (e->flags & 1) { // enabled
                if (out->lapic_count < MADT_MAX_LAPICS) {
                    out->lapic_ids[out->lapic_count] = e->apic_id;
                    if (out->lapic_count == 0)
                        out->lapic_bsp_id = e->apic_id; // first = BSP
                    out->lapic_count++;
                }
            }
        } else if (type == MADT_TYPE_IOAPIC && len >= 12) {
            volatile madt_ioapic_t *e = (volatile madt_ioapic_t *)p;
            if (out->ioapic_count < MADT_MAX_IOAPICS) {
                out->ioapic_base[out->ioapic_count] = e->mmio_addr;
                out->ioapic_gsi_base[out->ioapic_count] = e->gsi_base;
                out->ioapic_count++;
            }
        }
        p += len;
    }

    out->valid = (out->lapic_count > 0);

    kprintf("[MADT] %u CPU(s), %u IOAPIC(s), LAPIC MMIO=0x%llx\n",
            out->lapic_count, out->ioapic_count,
            (unsigned long long)lapic_mmio);
    for (u32 i = 0; i < out->lapic_count; i++) {
        kprintf("[MADT]   CPU %u: APIC ID %d%s\n", i, out->lapic_ids[i],
                i == 0 ? " (BSP)" : "");
    }

    return out->valid;
}
