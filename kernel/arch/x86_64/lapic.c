/* =============================================================================
 * XIU Operating System — x86_64 Local APIC (LAPIC) Driver
 * kernel/arch/x86_64/lapic.c
 *
 * Implements Local APIC initialization, EOI, and Inter-Processor Interrupts (IPI)
 * ============================================================================= */

#include <kernel/lapic.h>
#include <kernel/xiu_types.h>

extern void kprintf(const char *fmt, ...);

#define MSR_IA32_APIC_BASE      0x0000001B
#define APIC_BASE_BSP           (1ULL << 8)
#define APIC_GLOBAL_ENABLE      (1ULL << 11)
#define APIC_BASE_ADDR_MASK     0x0000000FFFFFF000ULL

static u64 s_lapic_base_va = 0;

static inline u64 rdmsr(u32 msr) {
    u32 low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((u64)high << 32) | low;
}

static inline void wrmsr(u32 msr, u64 val) {
    u32 low = (u32)val;
    u32 high = (u32)(val >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline u32 lapic_read(u32 reg) {
    if (!s_lapic_base_va) return 0;
    return *(volatile u32 *)(s_lapic_base_va + reg);
}

static inline void lapic_write(u32 reg, u32 val) {
    if (!s_lapic_base_va) return;
    *(volatile u32 *)(s_lapic_base_va + reg) = val;
}

void lapic_init_bsp(void) {
    u64 apic_base_msr = rdmsr(MSR_IA32_APIC_BASE);
    
    // enable APIC globally in MSR
    if (!(apic_base_msr & APIC_GLOBAL_ENABLE)) {
        apic_base_msr |= APIC_GLOBAL_ENABLE;
        wrmsr(MSR_IA32_APIC_BASE, apic_base_msr);
    }

    u64 lapic_phys = apic_base_msr & APIC_BASE_ADDR_MASK;
    s_lapic_base_va = lapic_phys + HHDM_BASE;

    lapic_write(LAPIC_SVR, 0x100 | VECTOR_SPURIOUS);
    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_LVT_LINT0, 0x00000700);
    lapic_write(LAPIC_LVT_LINT1, 0x00000400);

    // acknowledge any pending interrupt
    lapic_write(LAPIC_EOI, 0);

    kprintf("  [  OK  ]  Local APIC (BSP ID=%u, Base=0x%llx)\n",
            lapic_get_id(), lapic_phys);
}

void lapic_init_ap(void) {
    u64 apic_base_msr = rdmsr(MSR_IA32_APIC_BASE);
    if (!(apic_base_msr & APIC_GLOBAL_ENABLE)) {
        apic_base_msr |= APIC_GLOBAL_ENABLE;
        wrmsr(MSR_IA32_APIC_BASE, apic_base_msr);
    }

    // enable in SVR on this AP core
    lapic_write(LAPIC_SVR, 0x100 | VECTOR_SPURIOUS);
    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_LVT_LINT0, 0x00010000); // masked on APs
    lapic_write(LAPIC_LVT_LINT1, 0x00010000);
    lapic_write(LAPIC_EOI, 0);
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

u32 lapic_get_id(void) {
    return (lapic_read(LAPIC_ID) >> 24) & 0xFF;
}

void lapic_send_ipi(u32 lapic_id, u8 vector) {
    lapic_write(LAPIC_ICR_HIGH, lapic_id << 24);
    lapic_write(LAPIC_ICR_LOW, ICR_FIXED | ICR_EDGE_TRIGGER | ICR_ASSERT | vector);

    // spin while delivery is pending
    while (lapic_read(LAPIC_ICR_LOW) & ICR_BUSY) {
        __asm__ volatile("pause");
    }
}

void lapic_send_ipi_all_excluding_self(u8 vector) {
    lapic_write(LAPIC_ICR_LOW, ICR_DEST_ALL_EXC_SELF | ICR_FIXED | ICR_EDGE_TRIGGER | ICR_ASSERT | vector);
    while (lapic_read(LAPIC_ICR_LOW) & ICR_BUSY) {
        __asm__ volatile("pause");
    }
}

void lapic_timer_init(u32 ticks) {
    lapic_write(LAPIC_TIMER_DIV_CFG, 0x3);
    lapic_write(LAPIC_LVT_TIMER, 0x20000 | 32);
    lapic_write(LAPIC_TIMER_INIT_CNT, ticks);
}
