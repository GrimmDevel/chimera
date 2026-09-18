/* =============================================================================
 * Chimera Operating System — Global Descriptor Table Implementation
 * kernel/arch/x86_64/gdt.c
 * ============================================================================= */

#include "gdt.h"
#include <kernel/smp.h>

static u64 gdt[7];
static struct tss_entry tss;

// FP state save configuration, set once by cpu_enable_features() on the BSP
// and consumed by switch.S (xsave/xrstor mask) and thread_init_stack (area)
u32 g_fpu_mask_lo = 0x3;
u32 g_fpu_mask_hi = 0;
u32 g_fpu_area_size = 512;

static u8 s_bsp_df_stack[4096] __attribute__((aligned(16)));
static u8 s_bsp_nmi_stack[4096] __attribute__((aligned(16)));

static u8 s_ap_df_stacks[CHIMERA_MAX_CPUS][4096] __attribute__((aligned(16)));
static u8 s_ap_nmi_stacks[CHIMERA_MAX_CPUS][4096] __attribute__((aligned(16)));

extern cpu_local_t g_cpu_data[CHIMERA_MAX_CPUS];

struct gdtr {
    u16 limit;
    u64 base;
} CHIMERA_PACKED;

static struct gdtr gdtr;

static void gdt_set_entry(int index, u32 base, u32 limit, u8 access, u8 flags) {
    gdt[index] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) |
                 ((u64)access << 40) | (((u64)limit & 0xF0000) << 32) |
                 ((u64)flags << 52) | (((u64)base & 0xFF000000) << 32);
}

extern void kprintf(const char *fmt, ...);

static inline void cpuid(u32 leaf, u32 subleaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

static void cpu_enable_features(void) {
    u64 cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2); // clear EM
    cr0 |= (1ULL << 1);  // set MP
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);  // OSFXSR
    cr4 |= (1ULL << 10); // OSXMMEXCPT

    // query CPUID for SMEP and SMAP support
    u32 eax = 0, ebx = 0, ecx = 0, edx = 0;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 7) {
        cpuid(7, 0, &eax, &ebx, &ecx, &edx);
        if (ebx & (1 << 7)) {
            cr4 |= (1ULL << 20); // SMEP
        }
        if (ebx & (1 << 20)) {
            cr4 |= (1ULL << 21); // SMAP
        }
    }
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    __asm__ volatile("fninit");

    // XSAVE machinery: without it fxsave silently loses YMM/AVX state on
    // context switch. Enable OSXSAVE, program XCR0 (x87|sse|avx), and export
    // the save mask + area size for switch.S / thread_init_stack.
    u32 eax1 = 0, ebx1 = 0, ecx1 = 0, edx1 = 0;
    cpuid(1, 0, &eax1, &ebx1, &ecx1, &edx1);
    bool has_xsave = (ecx1 >> 26) & 1;
    bool has_avx = (ecx1 >> 28) & 1;

    g_fpu_mask_lo = 0x3; // x87 | sse
    g_fpu_mask_hi = 0;
    g_fpu_area_size = 512;

    if (has_xsave) {
        cr4 |= (1ULL << 18); // OSXSAVE
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

        u32 xcr0 = 0x3;
        if (has_avx) xcr0 |= 0x4; // AVX state
        u32 lo = xcr0, hi = 0;
        __asm__ volatile("xsetbv" :: "c"(0), "a"(lo), "d"(hi) : "memory");

        // verify the CPU accepted the mask
        u32 vlo = 0, vhi = 0;
        __asm__ volatile("xgetbv" : "=a"(vlo), "=d"(vhi) : "c"(0));
        if ((vlo & xcr0) != xcr0) xcr0 = vlo & 0x3;
        if (has_avx && (xcr0 & 0x4) == 0) has_avx = false;

        g_fpu_mask_lo = xcr0;
        g_fpu_mask_hi = 0;

        u32 da = 0, db = 0, dc = 0, dd = 0;
        cpuid(0xD, 0, &da, &db, &dc, &dd);
        if (db > g_fpu_area_size) g_fpu_area_size = db;

        kprintf("        fpu: xsave mask=0x%x area=%u bytes%s\n", xcr0,
                g_fpu_area_size, has_avx ? " (AVX)" : "");
    } else {
        kprintf("        fpu: xsave unsupported — fxsave fallback\n");
    }
}

void gdt_init(void) {
    gdt[0] = 0;
    
    // kernel code 0x08
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xA);
    
    // kernel data 0x10
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC);
    
    // user data 0x18
    gdt_set_entry(3, 0, 0xFFFFF, 0xF2, 0xC);
    
    // user code 0x20
    gdt_set_entry(4, 0, 0xFFFFF, 0xFA, 0xA);
    
    // tss 0x28
    __builtin_memset(&tss, 0, sizeof(tss));
    tss.iopb_offset = sizeof(tss);
    tss.ist1 = (u64)(s_bsp_df_stack + sizeof(s_bsp_df_stack));
    tss.ist2 = (u64)(s_bsp_nmi_stack + sizeof(s_bsp_nmi_stack));
    
    u64 tss_base = (u64)&tss;
    u32 tss_limit = sizeof(tss) - 1;
    
    gdt[5] = (tss_limit & 0xFFFF) | ((tss_base & 0xFFFFFF) << 16) | 
             (0x89ULL << 40) | (((u64)tss_limit & 0xF0000) << 32) | 
             (0x00ULL << 52) | (((tss_base >> 24) & 0xFF) << 56);
    gdt[6] = (tss_base >> 32);
    
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (u64)gdt;
    
    __asm__ volatile (
        "lgdt %0\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "mov $0x28, %%ax\n"
        "ltr %%ax\n"
        :
        : "m"(gdtr)
        : "rax", "memory"
    );

    cpu_enable_features();
}

void tss_set_rsp0(u64 rsp0) {
    tss.rsp0 = rsp0;
}

void tss_set_rsp0_cpu(u32 cpu_id, u64 rsp0) {
    if (cpu_id == 0) {
        tss.rsp0 = rsp0;
    } else if (cpu_id < CHIMERA_MAX_CPUS && g_cpu_data[cpu_id].cpu_tss_ptr) {
        ((struct tss_entry *)g_cpu_data[cpu_id].cpu_tss_ptr)->rsp0 = rsp0;
    }
}

void gdt_init_ap(u64 *ap_gdt, struct tss_entry *ap_tss, u32 cpu_id) {
    if (!ap_gdt || !ap_tss) return;

    ap_gdt[0] = 0;
    ap_gdt[1] = (0xFFFFF & 0xFFFF) | ((0 & 0xFFFFFF) << 16) |
                ((u64)0x9A << 40) | (((u64)0xFFFFF & 0xF0000) << 32) |
                ((u64)0xA << 52) | (((u64)0 & 0xFF000000) << 32);
    ap_gdt[2] = (0xFFFFF & 0xFFFF) | ((0 & 0xFFFFFF) << 16) |
                ((u64)0x92 << 40) | (((u64)0xFFFFF & 0xF0000) << 32) |
                ((u64)0xC << 52) | (((u64)0 & 0xFF000000) << 32);
    ap_gdt[3] = (0xFFFFF & 0xFFFF) | ((0 & 0xFFFFFF) << 16) |
                ((u64)0xF2 << 40) | (((u64)0xFFFFF & 0xF0000) << 32) |
                ((u64)0xC << 52) | (((u64)0 & 0xFF000000) << 32);
    ap_gdt[4] = (0xFFFFF & 0xFFFF) | ((0 & 0xFFFFFF) << 16) |
                ((u64)0xFA << 40) | (((u64)0xFFFFF & 0xF0000) << 32) |
                ((u64)0xA << 52) | (((u64)0 & 0xFF000000) << 32);

    __builtin_memset(ap_tss, 0, sizeof(*ap_tss));
    ap_tss->iopb_offset = sizeof(*ap_tss);

    if (cpu_id < CHIMERA_MAX_CPUS) {
        ap_tss->ist1 = (u64)(s_ap_df_stacks[cpu_id] + sizeof(s_ap_df_stacks[cpu_id]));
        ap_tss->ist2 = (u64)(s_ap_nmi_stacks[cpu_id] + sizeof(s_ap_nmi_stacks[cpu_id]));
    }

    u64 tss_base = (u64)ap_tss;
    u32 tss_limit = sizeof(*ap_tss) - 1;

    ap_gdt[5] = (tss_limit & 0xFFFF) | ((tss_base & 0xFFFFFF) << 16) | 
                (0x89ULL << 40) | (((u64)tss_limit & 0xF0000) << 32) | 
                (0x00ULL << 52) | (((tss_base >> 24) & 0xFF) << 56);
    ap_gdt[6] = (tss_base >> 32);

    struct gdtr ap_gdtr;
    ap_gdtr.limit = 7 * sizeof(u64) - 1;
    ap_gdtr.base = (u64)ap_gdt;

    __asm__ volatile (
        "lgdt %0\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "mov $0x28, %%ax\n"
        "ltr %%ax\n"
        :
        : "m"(ap_gdtr)
        : "rax", "memory"
    );
    // NOTE: cpu_enable_features() is BSP-only — it writes global FP config
    // that the APs already inherit from the boot CR4/XCR0 state.
}
