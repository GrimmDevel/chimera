/* =============================================================================
 * XIU Operating System — Global Descriptor Table Implementation
 * kernel/arch/x86_64/gdt.c
 * ============================================================================= */

#include "gdt.h"

static u64 gdt[7];
static struct tss_entry tss;

struct gdtr {
    u16 limit;
    u64 base;
} XIU_PACKED;

static struct gdtr gdtr;

static void gdt_set_entry(int index, u32 base, u32 limit, u8 access, u8 flags) {
    gdt[index] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) |
                 ((u64)access << 40) | (((u64)limit & 0xF0000) << 32) |
                 ((u64)flags << 52) | (((u64)base & 0xFF000000) << 32);
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

    // enable fpu and sse
    u64 cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);
    cr0 |= (1ULL << 1);
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);
    cr4 |= (1ULL << 10);
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    __asm__ volatile("fninit");
}

void tss_set_rsp0(u64 rsp0) {
    tss.rsp0 = rsp0;
}

void tss_set_rsp0_cpu(u32 cpu_id, u64 rsp0) {
    (void)cpu_id;
    tss.rsp0 = rsp0;
}

void gdt_init_ap(u64 *ap_gdt, struct tss_entry *ap_tss) {
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

    // enable FPU and SSE on this AP core
    u64 cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2); // clear EM
    cr0 |= (1ULL << 1);  // set MP
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);  // set OSFXSR
    cr4 |= (1ULL << 10); // set OSXMMEXCPT
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    __asm__ volatile("fninit");
}
