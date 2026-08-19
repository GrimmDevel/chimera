/* =============================================================================
 * XIU Operating System — Symmetric Multiprocessing (SMP) Subsystem
 * kernel/arch/x86_64/smp.c
 *
 * Implements Application Processor (AP) booting via Limine SMP protocol,
 * per-CPU state configuration, and cross-core IPI communication.
 * ============================================================================= */

#include <kernel/smp.h>
#include <kernel/lapic.h>
#include <kernel/proc.h>
#include <arch/x86_64/gdt.h>
#include <limine/limine.h>

extern void kprintf(const char *fmt, ...);
extern xiu_paddr_t pmm_alloc_pages(usize count);
extern void scheduler_ap_run(void);
extern void idt_reload(void);

// msr definitions
#define MSR_EFER            0xC0000080
#define MSR_STAR            0xC0000081
#define MSR_LSTAR           0xC0000082
#define MSR_SFMASK          0xC0000084
#define MSR_GS_BASE         0xC0000101
#define MSR_KERNEL_GS_BASE  0xC0000102

#define EFER_SCE            (1ULL << 0)  /* System Call Extensions */

extern void x86_64_syscall_entry(void);

// per-CPU Data Blocks
cpu_local_t g_cpu_data[XIU_MAX_CPUS];
u32 g_active_cpus = 1;
static u32 s_total_cpus = 1;

static u64 s_ap_gdt[XIU_MAX_CPUS][7];
static struct tss_entry s_ap_tss[XIU_MAX_CPUS];

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

static void smp_setup_syscall(cpu_local_t *cpu) {
    // 1. Enable SCE in EFER
    u64 efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);

    // 2. STAR MSR: Target CS/SS for syscall and sysret
    u64 star = ((u64)0x18 | 3ULL) << 48 | ((u64)0x08) << 32;
    wrmsr(MSR_STAR, star);

    // 3. LSTAR: RIP entry point
    wrmsr(MSR_LSTAR, (u64)x86_64_syscall_entry);

    // 4. SFMASK: Mask IF and TF
    wrmsr(MSR_SFMASK, 0x00000300ULL);

    // 5. GS Base
    wrmsr(MSR_GS_BASE, (u64)cpu);
    wrmsr(MSR_KERNEL_GS_BASE, 0);
}

void smp_ap_entry(struct limine_smp_info *info) {
    cpu_local_t *cpu = (cpu_local_t *)info->extra_argument;
    if (!cpu) return;

    // 1. initialize per-cpu gdt, tss, fpu
    gdt_init_ap(cpu->cpu_gdt_ptr, (struct tss_entry *)cpu->cpu_tss_ptr);

    // 2. Load IDT
    idt_reload();

    // 3. Setup Syscall and GS base
    smp_setup_syscall(cpu);

    // 4. Initialize Local APIC on this core
    lapic_init_ap();

    // 5. Mark CPU online
    cpu->cpu_is_active = 1;
    __atomic_fetch_add(&g_active_cpus, 1, __ATOMIC_SEQ_CST);

    kprintf("  [  OK  ]  SMP: Core %u online (LAPIC ID=%u)\n",
            cpu->cpu_id, cpu->cpu_lapic_id);

    // 6. Enter SMP Scheduler
    scheduler_ap_run();
}

extern volatile struct limine_smp_request smp_request;

void smp_init(void) {
    // 1. bsp setup
    __builtin_memset(g_cpu_data, 0, sizeof(g_cpu_data));
    g_cpu_data[0].cpu_id = 0;
    g_cpu_data[0].cpu_lapic_id = lapic_get_id();
    g_cpu_data[0].cpu_is_bsp = 1;
    g_cpu_data[0].cpu_is_active = 1;
    g_active_cpus = 1;

    // ensure BSP GS base is set to g_cpu_data[0]
    wrmsr(MSR_GS_BASE, (u64)&g_cpu_data[0]);
    wrmsr(MSR_KERNEL_GS_BASE, 0);

    // 2. Initialize BSP Local APIC
    lapic_init_bsp();

    // 3. Check Limine SMP Response
    if (!smp_request.response) {
        kprintf("[SMP] Limine SMP response not found — running uniprocessor\n");
        return;
    }

    struct limine_smp_response *resp = smp_request.response;
    s_total_cpus = (u32)resp->cpu_count;
    if (s_total_cpus > XIU_MAX_CPUS) s_total_cpus = XIU_MAX_CPUS;

    kprintf("[SMP] Discovered %u CPU cores via Limine SMP\n", s_total_cpus);

    // 4. boot ap cores
    for (u32 i = 0; i < s_total_cpus; i++) {
        struct limine_smp_info *info = resp->cpus[i];
        if (info->lapic_id == resp->bsp_lapic_id) {
            g_cpu_data[0].cpu_lapic_id = info->lapic_id;
            continue;
        }

        u32 cpu_idx = i;
        g_cpu_data[cpu_idx].cpu_id = cpu_idx;
        g_cpu_data[cpu_idx].cpu_lapic_id = info->lapic_id;
        g_cpu_data[cpu_idx].cpu_is_bsp = 0;
        g_cpu_data[cpu_idx].cpu_is_active = 0;
        g_cpu_data[cpu_idx].cpu_gdt_ptr = s_ap_gdt[cpu_idx];
        g_cpu_data[cpu_idx].cpu_tss_ptr = &s_ap_tss[cpu_idx];

        // allocate 16 KB kernel stack for this AP
        xiu_paddr_t stack_phys = pmm_alloc_pages(4);
        if (stack_phys) {
            u64 stack_top = stack_phys + HHDM_BASE + (4 * 4096);
            g_cpu_data[cpu_idx].cpu_kernel_stack = (void *)stack_top;
            s_ap_tss[cpu_idx].rsp0 = stack_top;
        }

        info->extra_argument = (u64)&g_cpu_data[cpu_idx];
        
        // signal Limine to jump AP to smp_ap_entry
        __atomic_store_n(&info->goto_address, (limine_goto_address)smp_ap_entry, __ATOMIC_RELEASE);

        // wait up to 50ms for AP to come online
        for (int spin = 0; spin < 50000; spin++) {
            if (g_cpu_data[cpu_idx].cpu_is_active) break;
            for (volatile int d = 0; d < 1000; d++);
        }
    }

    kprintf("  [  OK  ]  SMP: %u / %u CPU cores active and running\n",
            g_active_cpus, s_total_cpus);
}

u32 smp_get_cpu_count(void) {
    return s_total_cpus;
}

u32 smp_get_active_cpus(void) {
    return g_active_cpus;
}

u32 smp_current_cpu_id(void) {
    u32 id = 0;
    __asm__ volatile("mov %%gs:0x10, %0" : "=r"(id));
    return id;
}

void smp_send_reschedule(u32 cpu_id) {
    if (cpu_id < s_total_cpus && g_cpu_data[cpu_id].cpu_is_active) {
        lapic_send_ipi(g_cpu_data[cpu_id].cpu_lapic_id, VECTOR_IPI_SCHED);
    }
}

void smp_broadcast_reschedule(void) {
    lapic_send_ipi_all_excluding_self(VECTOR_IPI_SCHED);
}

void smp_tlb_shootdown(void) {
    lapic_send_ipi_all_excluding_self(VECTOR_IPI_TLB);
    // invalidate local TLB
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0; mov %0, %%cr3" : "=r"(cr3));
}
