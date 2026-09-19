// SMP bring-up: MADT parsing → SIPI → per-CPU init
// kernel/arch/x86_64/smp.c
//
// Replaces the stub smp_init with real AP bring-up:
//   1. Parse ACPI MADT to find AP cores
//   2. Copy the AP trampoline to low memory (0x7000)
//   3. Build identity page tables at 0x8000 (PML4) / 0x9000 (PDPT)
//   4. Send INIT + SIPI to each AP
//   5. APs enter smp_ap_entry_64 with per-CPU data and enter the scheduler

#include <kernel/smp.h>
#include <kernel/lapic.h>
#include <kernel/proc.h>
#include <arch/x86_64/gdt.h>
#include "madt.h"

extern void kprintf(const char *fmt, ...);
extern chimera_paddr_t pmm_alloc_pages(usize count);
extern u64 g_hhdm_base;
extern u32 g_cpu_count;
extern void scheduler_ap_run(void);
extern void idt_reload(void);
extern void cpu_init_syscall(void);

// AP trampoline (from ap_trampoline.S)
extern char ap_tramp_start[];
extern char ap_tramp_end[];

// low-memory addresses for the trampoline + page tables
#define AP_TRAMPOLINE_PHYS   0x7000UL
#define AP_IDENTITY_PML4     0x8000UL
#define AP_IDENTITY_PDPT     0x9000UL

// per-AP data slots written into the trampoline data area
#define AP_DATA_STACK        (AP_TRAMPOLINE_PHYS + 0x340)
#define AP_DATA_CPU_LOCAL    (AP_TRAMPOLINE_PHYS + 0x348)
#define AP_DATA_C_ENTRY      (AP_TRAMPOLINE_PHYS + 0x350)
#define AP_DATA_ALIVE        (AP_TRAMPOLINE_PHYS + 0x358)
#define AP_DATA_KERNEL_CR3   (AP_TRAMPOLINE_PHYS + 0x360)

static madt_info_t s_madt;
static _Atomic(u32) s_aps_started = 0;
static u32 s_total_cpus = 1;
static u32 s_bsp_cr4 = 0;
static u64 s_ap_gdt[CHIMERA_MAX_CPUS][7];
static struct tss_entry s_ap_tss[CHIMERA_MAX_CPUS];
static _Atomic(u32) s_cpu_state[CHIMERA_MAX_CPUS];

typedef struct smp_boot_diag {
    u64 gs_base;
    u64 cr3;
    u64 rsp;
} smp_boot_diag_t;

static smp_boot_diag_t s_boot_diag[CHIMERA_MAX_CPUS];
u32 g_active_cpus = 1;
volatile bool g_smp_ready = false; // APs spin-wait until BSP finishes booting
cpu_local_t g_cpu_data[CHIMERA_MAX_CPUS];

static void smp_set_cpu_state(u32 cpu_id, smp_cpu_state_t state) {
    if (cpu_id < CHIMERA_MAX_CPUS)
        atomic_store_explicit(&s_cpu_state[cpu_id], (u32)state,
                              memory_order_release);
}

smp_cpu_state_t smp_get_cpu_state(u32 cpu_id) {
    if (cpu_id >= CHIMERA_MAX_CPUS) return SMP_CPU_FAILED;
    return (smp_cpu_state_t)atomic_load_explicit(&s_cpu_state[cpu_id],
                                                  memory_order_acquire);
}


static inline u64 smp_read_msr(u32 msr) {
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

static inline void smp_write_msr(u32 msr, u64 value) {
    u32 lo = (u32)value;
    u32 hi = (u32)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

static void smp_capture_boot_diag(u32 cpu_id) {
    if (cpu_id >= CHIMERA_MAX_CPUS) return;
    u64 cr3, rsp;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    s_boot_diag[cpu_id].gs_base = smp_read_msr(0xC0000101);
    s_boot_diag[cpu_id].cr3 = cr3;
    s_boot_diag[cpu_id].rsp = rsp;
}

// 64-bit AP entry — called from the trampoline with RDI = cpu_local_t*
void smp_ap_entry_64(cpu_local_t *cpu) {
    // 0. Restore the BSP's full CR0, CR4, and XCR0 (the trampoline only set
    //    minimal state; the AP needs OSXSAVE + XCR0 programmed for xsave/xrstor,
    //    OSFXSR for SSE, SMEP, SMAP)
    {
        u64 cr0;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 &= ~(1ULL << 2); // clear EM
        cr0 |= (1ULL << 1);  // set MP
        __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");

        u64 cr4v = s_bsp_cr4;
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4v) : "memory");

        __asm__ volatile("fninit");

        if (cr4v & (1ULL << 18)) {
            extern u32 g_fpu_mask_lo;
            extern u32 g_fpu_mask_hi;
            u32 lo = g_fpu_mask_lo;
            u32 hi = g_fpu_mask_hi;
            __asm__ volatile("xsetbv" :: "c"(0), "a"(lo), "d"(hi) : "memory");
        }
    }

    // 1. Per-CPU GDT/TSS (already allocated by the BSP)
    gdt_init_ap(cpu->cpu_gdt_ptr, (struct tss_entry *)cpu->cpu_tss_ptr, cpu->cpu_id);
    idt_reload();

    // gdt_init_ap reloads the GS selector. Reinstall the architectural GS
    // base afterwards so gs:[0] consistently resolves to this CPU's local
    // state in both interrupt and scheduler paths.
    smp_write_msr(0xC0000101, (u64)cpu);
    smp_write_msr(0xC0000102, 0);

    // 2. Syscall MSRs for this CPU
    cpu_init_syscall();

    // 3. Per-CPU LAPIC
    lapic_init_ap();

    smp_capture_boot_diag(cpu->cpu_id);
    smp_set_cpu_state(cpu->cpu_id, SMP_CPU_ONLINE_IDLE);

    kprintf("  [  OK  ]  SMP: Core %u online (LAPIC ID=%u, awaiting scheduler)\n",
            cpu->cpu_id, cpu->cpu_lapic_id);
    kprintf("        ap%u: GS=0x%016llx CR3=0x%016llx RSP=0x%016llx\n",
            cpu->cpu_id, (unsigned long long)s_boot_diag[cpu->cpu_id].gs_base,
            (unsigned long long)s_boot_diag[cpu->cpu_id].cr3,
            (unsigned long long)s_boot_diag[cpu->cpu_id].rsp);

    u64 cr3_ap;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3_ap));
    atomic_store_explicit(&cpu->cpu_active_cr3, cr3_ap & 0x000FFFFFFFFFF000ULL, memory_order_release);

    // Spin-wait until BSP completes kernel initialization and sets g_smp_ready
    while (!__atomic_load_n(&g_smp_ready, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }

    cpu->cpu_is_active = 1;
    smp_set_cpu_state(cpu->cpu_id, SMP_CPU_SCHEDULABLE);
    __atomic_fetch_add(&g_active_cpus, 1, __ATOMIC_RELEASE);

    // Start local LAPIC periodic timer for timeslices (10ms)
    extern u32 lapic_timer_get_ticks_per_10ms(void);
    lapic_timer_start_periodic(lapic_timer_get_ticks_per_10ms());

    // Enter per-CPU scheduler
    scheduler_ap_run();
}

// write a qword to a physical address via HHDM
static inline void phys_write64(u64 phys, u64 val) {
    *(volatile u64 *)(phys + g_hhdm_base) = val;
}

// build identity page tables (PML4 at 0x8000, PDPT at 0x9000)
// maps the first 4GB as 1GB pages so the AP can access all RAM and MMIO
static void build_identity_page_tables(void) {
    // PML4: entry 0 → PDPT at 0x9000 (present, write, user)
    phys_write64(AP_IDENTITY_PML4, AP_IDENTITY_PDPT | 0x03);
    // entries 1-511 = 0 (not present)
    for (u32 i = 1; i < 512; i++) {
        phys_write64(AP_IDENTITY_PML4 + i * 8, 0);
    }

    // PDPT: entries 0-3 → 1GB identity pages (present, write, user, PS=1GB)
    for (u32 i = 0; i < 4; i++) {
        phys_write64(AP_IDENTITY_PDPT + i * 8, (u64)i * 0x40000000UL | 0x83);
    }
    for (u32 i = 4; i < 512; i++) {
        phys_write64(AP_IDENTITY_PDPT + i * 8, 0);
    }
}

// copy the trampoline blob to low memory and set up the identity tables
static void setup_trampoline(void) {
    // copy the trampoline code blob to 0x7000
    usize tramp_size = (usize)(ap_tramp_end - ap_tramp_start);
    volatile u8 *dst = (volatile u8 *)(AP_TRAMPOLINE_PHYS + g_hhdm_base);
    for (usize i = 0; i < tramp_size; i++) {
        dst[i] = ap_tramp_start[i];
    }

    // the GDT descriptor base in the blob is linked as 0x7300 — since the
    // blob is copied to 0x7000, all absolute references are already correct

    // build identity page tables
    build_identity_page_tables();

    // store the current kernel CR3 for the AP to switch to after long mode
    u64 kernel_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(kernel_cr3));
    phys_write64(AP_TRAMPOLINE_PHYS + 0x360, kernel_cr3);
}

// set up per-AP data in the trampoline for the next AP to start
static void set_ap_data(u32 cpu_slot, u64 stack_top, u64 cpu_local, u64 c_entry) {
    (void)cpu_slot;
    phys_write64(AP_DATA_STACK, stack_top);
    phys_write64(AP_DATA_CPU_LOCAL, cpu_local);
    phys_write64(AP_DATA_C_ENTRY, c_entry);
    phys_write64(AP_DATA_ALIVE, 0);
    // kernel CR3 is written once in setup_trampoline
}

extern u64 timer_tsc_hz(void);

void smp_delay_us(u32 us) {
    u64 hz = timer_tsc_hz();
    if (hz == 0) {
        for (volatile u32 i = 0; i < us * 50; i++) __asm__ volatile("pause");
        return;
    }
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    u64 start = ((u64)hi << 32) | lo;
    u64 target = (hz / 1000000ULL) * (u64)us;
    while (1) {
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        u64 now = ((u64)hi << 32) | lo;
        if (now - start >= target) break;
        __asm__ volatile("pause");
    }
}

// send INIT + SIPI to an AP with the given LAPIC ID
static void send_init_sipi(u8 lapic_id) {
    // INIT IPI: level assert, delivery mode 101 (INIT)
    lapic_write(LAPIC_ICR_HIGH, (u32)lapic_id << 24);
    lapic_write(LAPIC_ICR_LOW, 0x00004500); // INIT, assert

    // wait 10ms for INIT to complete
    smp_delay_us(10000);

    // SIPI #1: vector = trampoline page (0x7000 >> 12 = 0x07), delivery 110
    lapic_write(LAPIC_ICR_HIGH, (u32)lapic_id << 24);
    lapic_write(LAPIC_ICR_LOW, 0x00004607); // STARTUP, vector 0x07

    // wait 200μs
    smp_delay_us(200);

    // SIPI #2 (backup only if first was missed)
    if (__atomic_load_n((const u64 *)(AP_DATA_ALIVE + g_hhdm_base), __ATOMIC_ACQUIRE) != 1) {
        lapic_write(LAPIC_ICR_HIGH, (u32)lapic_id << 24);
        lapic_write(LAPIC_ICR_LOW, 0x00004607);
        smp_delay_us(200);
    }
}

void smp_init(void) {
    // 1. BSP setup
    __builtin_memset(g_cpu_data, 0, sizeof(g_cpu_data));
    atomic_store_explicit(&s_aps_started, 0, memory_order_relaxed);
    for (u32 i = 0; i < CHIMERA_MAX_CPUS; i++)
        atomic_store_explicit(&s_cpu_state[i], SMP_CPU_OFFLINE,
                              memory_order_relaxed);
    g_cpu_data[0].cpu_id = 0;
    g_cpu_data[0].cpu_lapic_id = lapic_get_id();
    g_cpu_data[0].cpu_is_bsp = 1;
    g_cpu_data[0].cpu_is_active = 1;
    smp_set_cpu_state(0, SMP_CPU_SCHEDULABLE);
    {
        u64 bsp_cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(bsp_cr3));
        atomic_store_explicit(&g_cpu_data[0].cpu_active_cr3, bsp_cr3 & 0x000FFFFFFFFFF000ULL, memory_order_release);
    }

    // BSP kernel stack
    chimera_paddr_t bsp_stack_phys = pmm_alloc_pages(4);
    if (bsp_stack_phys && bsp_stack_phys != (chimera_paddr_t)-1) {
        u64 stack_top = bsp_stack_phys + g_hhdm_base + (4 * 4096);
        g_cpu_data[0].cpu_kernel_stack = (void *)stack_top;
    }
    g_active_cpus = 1;

    // ensure BSP GS base is set
    __asm__ volatile("mov %0, %%gs:0" :: "r"((u64)&g_cpu_data[0]) : "memory");
    // Actually — the GS base is set via MSR, not a mov. Keep the existing
    // setup. The wrmsr below matches scheduler_init's approach.
    {
        u32 lo = (u32)(u64)&g_cpu_data[0];
        u32 hi = (u32)(((u64)&g_cpu_data[0]) >> 32);
        __asm__ volatile("wrmsr" :: "c"(0xC0000101), "a"(lo), "d"(hi));
    }
    {
        u32 lo = 0, hi = 0;
        __asm__ volatile("wrmsr" :: "c"(0xC0000102), "a"(lo), "d"(hi));
    }

    // save the BSP's fully configured CR4 — APs start with bare CR4 (only
    // PAE from the trampoline) and need the full feature set (OSXSAVE for
    // xsave, OSFXSR for SSE, SMEP, SMAP)
    { u64 cr4v; __asm__ volatile("mov %%cr4, %0" : "=r"(cr4v)); s_bsp_cr4 = (u32)cr4v; }

    // 2. Initialise BSP LAPIC and calibrate LAPIC timer
    lapic_init_bsp();
    lapic_timer_calibrate();

    // 3. Parse ACPI MADT to find AP cores
    extern u64 g_boot_rsdp_base;  // set in chimera_kernel_main from boot info
    if (!madt_parse(g_boot_rsdp_base, &s_madt)) {
        kprintf("  [  OK  ]  SMP: 1 CPU (no MADT — single-core fallback)\n");
        s_total_cpus = 1;
        return;
    }

    s_total_cpus = s_madt.lapic_count;
    if (s_total_cpus > CHIMERA_MAX_CPUS) {
        kprintf("[SMP] Limiting MADT topology from %u to %u CPUs\n",
                s_total_cpus, CHIMERA_MAX_CPUS);
        s_total_cpus = CHIMERA_MAX_CPUS;
    }
    g_cpu_count = s_total_cpus;

    if (s_total_cpus <= 1) {
        kprintf("  [  OK  ]  SMP: 1 CPU (single-core system)\n");
        return;
    }

    // 4. Set up per-AP data structures (skip BSP at index 0)
    for (u32 i = 1; i < s_total_cpus && i < MADT_MAX_LAPICS; i++) {
        cpu_local_t *cpu = &g_cpu_data[i];
        cpu->cpu_id = i;
        cpu->cpu_lapic_id = s_madt.lapic_ids[i];
        cpu->cpu_is_bsp = 0;
        cpu->cpu_is_active = 0;
        smp_set_cpu_state(i, SMP_CPU_STARTING);
        cpu->cpu_gdt_ptr = (void *)s_ap_gdt[i];
        cpu->cpu_tss_ptr = (void *)&s_ap_tss[i];

        // per-AP kernel stack
        chimera_paddr_t stack_phys = pmm_alloc_pages(4);
        if (stack_phys && stack_phys != (chimera_paddr_t)-1) {
            cpu->cpu_kernel_stack = (void *)(stack_phys + g_hhdm_base + (4 * 4096));
        }
    }

    // 5. Copy the trampoline to low memory + build identity page tables
    setup_trampoline();

    // 6. Send INIT + SIPI to each AP
    kprintf("[SMP] Starting %u AP cores...\n", s_total_cpus - 1);
    for (u32 i = 1; i < s_total_cpus && i < MADT_MAX_LAPICS; i++) {
        cpu_local_t *cpu = &g_cpu_data[i];
        if (!cpu->cpu_kernel_stack) {
            smp_set_cpu_state(i, SMP_CPU_FAILED);
            kprintf("[SMP] AP %u: no kernel stack; skipped\n", i);
            continue;
        }

        // write the per-AP data into the trampoline
        set_ap_data(i, (u64)cpu->cpu_kernel_stack,
                    (u64)cpu, // cpu_local_t pointer (GS base)
                    (u64)smp_ap_entry_64);

        kprintf("[SMP] Sending SIPI to AP %u (LAPIC ID=%u)...\n",
                i, cpu->cpu_lapic_id);
        send_init_sipi(cpu->cpu_lapic_id);

        // wait for the AP to signal alive (up to 100ms)
        bool alive = false;
        for (int timeout = 0; timeout < 1000; timeout++) {
            if (__atomic_load_n((const u64 *)(AP_DATA_ALIVE + g_hhdm_base), __ATOMIC_ACQUIRE) == 1) {
                atomic_fetch_add_explicit(&s_aps_started, 1,
                                          memory_order_relaxed);
                alive = true;
                break;
            }
            smp_delay_us(100);
        }
        if (!alive) {
            smp_set_cpu_state(i, SMP_CPU_FAILED);
            kprintf("[SMP] AP %u (LAPIC ID=%u) did not acknowledge SIPI: timeout waiting for ap_alive in trampoline\n",
                    i, cpu->cpu_lapic_id);
        }
    }

    kprintf("  [  OK  ]  SMP stage 1: BSP schedulable, %u / %u AP(s) online-idle\n",
            atomic_load_explicit(&s_aps_started, memory_order_acquire),
            s_total_cpus - 1);
}

u32 smp_get_cpu_count(void) { return s_total_cpus; }
u32 smp_get_active_cpus(void) { return g_active_cpus; }
u32 smp_current_cpu_id(void) {
    u32 id = 0;
    __asm__ volatile("mov %%gs:0x10, %0" : "=r"(id));
    return id;
}

void smp_send_reschedule(u32 cpu_id) {
    if (cpu_id >= s_total_cpus) return;
    atomic_store_explicit(&g_cpu_data[cpu_id].cpu_need_resched, 1, memory_order_release);
    u32 my_cpu = smp_current_cpu_id();
    if (cpu_id != my_cpu && g_cpu_data[cpu_id].cpu_is_active) {
        lapic_send_ipi(g_cpu_data[cpu_id].cpu_lapic_id, VECTOR_IPI_SCHED);
    }
}


static spinlock_t s_tlb_lock = SPINLOCK_INIT;
static volatile u64 s_tlb_req_cr3 = 0;
static volatile u64 s_tlb_req_va = 0;
static volatile usize s_tlb_req_size = 0;
static _Atomic(u32) s_tlb_ack_mask = 0;

void smp_tlb_ipi_handler(void) {
    u32 my_cpu = smp_current_cpu_id();
    u64 req_cr3 = s_tlb_req_cr3;
    u64 req_va = s_tlb_req_va;
    usize req_size = s_tlb_req_size;

    u64 my_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(my_cr3));

    if (req_cr3 == 0 || (my_cr3 & 0x000FFFFFFFFFF000ULL) == (req_cr3 & 0x000FFFFFFFFFF000ULL)) {
        if (req_va == 0 || req_size == 0 || req_size >= (2 * 1024 * 1024)) {
            __asm__ volatile("mov %0, %%cr3" :: "r"(my_cr3) : "memory");
        } else {
            u64 end_va = req_va + req_size;
            for (u64 va = (req_va & ~0xFFFULL); va < end_va; va += 4096) {
                __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
            }
        }
    }

    if (my_cpu < CHIMERA_MAX_CPUS) {
        atomic_fetch_and_explicit(&s_tlb_ack_mask, ~(1u << my_cpu), memory_order_release);
    }
}

void smp_tlb_flush_range_pml4(u64 pml4_phys, u64 start_va, usize size) {
    u64 my_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(my_cr3));
    bool is_my_cr3 = (pml4_phys == 0 || (my_cr3 & 0x000FFFFFFFFFF000ULL) == (pml4_phys & 0x000FFFFFFFFFF000ULL));
    if (is_my_cr3) {
        if (start_va == 0 || size == 0 || size >= (2 * 1024 * 1024)) {
            __asm__ volatile("mov %0, %%cr3" :: "r"(my_cr3) : "memory");
        } else {
            u64 end_va = start_va + size;
            for (u64 va = (start_va & ~0xFFFULL); va < end_va; va += 4096) {
                __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
            }
        }
    }

    u32 my_cpu = smp_current_cpu_id();
    u32 total = smp_get_cpu_count();
    if (total <= 1 || g_active_cpus <= 1) return;

    u32 target_mask = 0;
    for (u32 i = 0; i < total && i < CHIMERA_MAX_CPUS; i++) {
        if (i == my_cpu || !g_cpu_data[i].cpu_is_active) continue;
        if (pml4_phys == 0) {
            target_mask |= (1u << i);
        } else {
            u64 c_cr3 = atomic_load_explicit(&g_cpu_data[i].cpu_active_cr3, memory_order_acquire);
            if ((c_cr3 & 0x000FFFFFFFFFF000ULL) == (pml4_phys & 0x000FFFFFFFFFF000ULL)) {
                target_mask |= (1u << i);
            }
        }
    }

    if (target_mask == 0) {
        return;
    }

    irq_flags_t flags = spinlock_lock_irqsave(&s_tlb_lock);

    s_tlb_req_cr3 = pml4_phys;
    s_tlb_req_va = start_va;
    s_tlb_req_size = size;
    atomic_store_explicit(&s_tlb_ack_mask, target_mask, memory_order_release);

    for (u32 i = 0; i < total && i < CHIMERA_MAX_CPUS; i++) {
        if (target_mask & (1u << i)) {
            lapic_send_ipi(g_cpu_data[i].cpu_lapic_id, VECTOR_IPI_TLB);
        }
    }

    u32 timeout = 2000000;
    while (atomic_load_explicit(&s_tlb_ack_mask, memory_order_acquire) != 0) {
        __asm__ volatile("pause");
        if (--timeout == 0) {
            break;
        }
    }

    spinlock_unlock_irqrestore(&s_tlb_lock, flags);
}

void smp_tlb_flush_page_pml4(u64 pml4_phys, u64 va) {
    smp_tlb_flush_range_pml4(pml4_phys, va, 4096);
}

void smp_tlb_shootdown_pml4(u64 pml4_phys) {
    smp_tlb_flush_range_pml4(pml4_phys, 0, 0);
}

void smp_tlb_flush_page(u64 va) {
    smp_tlb_flush_page_pml4(0, va);
}

void smp_tlb_flush_range(u64 start_va, usize size) {
    smp_tlb_flush_range_pml4(0, start_va, size);
}

void smp_tlb_shootdown(void) {
    smp_tlb_shootdown_pml4(0);
}
