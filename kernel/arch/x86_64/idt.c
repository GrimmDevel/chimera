#include "idt.h"
#include <kernel/io.h>
#include <kernel/panic.h>
#include <kernel/proc.h>

extern void kprintf(const char *fmt, ...);

volatile u64 g_system_ticks = 0;

u64 timer_get_uptime_seconds(void) {
    return g_system_ticks / 100;
}

static struct idt_entry idt[256];
static struct idtr idtr;

extern void* interrupt_handlers[];

void idt_set_gate(u8 vector, void *handler, u8 flags) {
    u64 addr = (u64)handler;
    idt[vector].base_low = addr & 0xFFFF;
    idt[vector].selector = 0x08; // kernel Code Segment
    idt[vector].ist = 0;
    idt[vector].flags = flags;
    idt[vector].base_mid = (addr >> 16) & 0xFFFF;
    idt[vector].base_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;
}

static void pic_remap(void) {
    // icw1: Start initialization
    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    outb(0x21, 0x20);
    outb(0xA1, 0x28);

    // icw3: cascading
    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    // icw4: 8086 mode
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, 0xF8);
    outb(0xA1, 0xEF);
}

static void pit_init(void) {
    const u32 divisor = 1193182 / 100;

    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

void idt_init(void) {
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, interrupt_handlers[i], 0x8E);
    }

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (u64)idt;

    __asm__ volatile("lidt %0" : : "m"(idtr));
    
    pit_init();
    pic_remap();
    
    kprintf("  [  OK  ]  Architecture IDT & PIC\n");
}

void idt_reload(void) {
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

// c-level interrupt dispatcher
struct interrupt_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 int_no, err_code;
    u64 rip, cs, rflags, rsp, ss;
};

void xiukit_hid_irq_handler(void);
extern void scheduler_yield(void);
extern void lapic_eoi(void);

static u64 read_cr2(void) {
    u64 cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

void interrupt_handler(struct interrupt_frame *frame) {
    if (frame->int_no == 0xEE) { // vector_ipi_sched
        lapic_eoi();
        if (current_thread() != nullptr) {
            scheduler_yield();
        }
        return;
    } else if (frame->int_no == 0xEF) { // vector_ipi_tlb
        lapic_eoi();
        u64 cr3;
        __asm__ volatile("mov %%cr3, %0; mov %0, %%cr3" : "=r"(cr3));
        return;
    } else if (frame->int_no == 0xFF) { // vector_spurious
        return;
    } else if (frame->int_no == 32) { // irq 0: PIT timer / LAPIC timer
        extern volatile u64 g_system_ticks;
        g_system_ticks++;

        extern void xiukit_hid_poll(void);
        xiukit_hid_poll();
        outb(0x20, 0x20);
        lapic_eoi();
        if (current_thread() != nullptr) {
            scheduler_yield();
        }
    } else if (frame->int_no == 44) {
        xiukit_hid_irq_handler();
        outb(0xA0, 0x20);
        outb(0x20, 0x20);
        lapic_eoi();
    } else if (frame->int_no == 33) {
        xiukit_hid_irq_handler();
        outb(0x20, 0x20);
        lapic_eoi();
    } else if (frame->int_no >= 32 && frame->int_no <= 47) {
        if (frame->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
        lapic_eoi();
    } else if (frame->int_no < 32) {
        bool is_user = ((frame->cs & 3) == 3);
        xiu_task_t *task = current_task();
        xiu_proc_t *proc = task ? task->ta_proc : nullptr;

        if (frame->int_no == 14) {
            u64 cr2 = read_cr2();

            // copy-on-write fault
            if (is_user && task && task->ta_vm_map && (frame->err_code & 2)) {
                extern u64 *pmap_get_pte_ptr(u64 pml4_phys, u64 vaddr);
                extern xiu_paddr_t pmm_alloc_page(void);
                extern void pmm_release_page(xiu_paddr_t addr);
                extern u16 pmm_get_refcount(xiu_paddr_t addr);

                u64 *pte_ptr = pmap_get_pte_ptr((u64)task->ta_vm_map, cr2);
                if (pte_ptr && (*pte_ptr & (1ULL << 0)) && (*pte_ptr & (1ULL << 9))) {
                    u64 old_phys = *pte_ptr & ~0xFFFULL;
                    u16 refcount = pmm_get_refcount(old_phys);

                    if (refcount > 1) {
                        u64 new_phys = pmm_alloc_page();
                        if (new_phys != (u64)-1) {
                            void *src_ptr = (void *)(old_phys + g_hhdm_base);
                            void *dst_ptr = (void *)(new_phys + g_hhdm_base);
                            __builtin_memcpy(dst_ptr, src_ptr, 4096);

                            pmm_release_page(old_phys);
                            *pte_ptr = (new_phys & ~0xFFFULL) | (*pte_ptr & 0xFFFULL) | (1ULL << 1);
                            *pte_ptr &= ~(1ULL << 9);

                            __asm__ volatile("invlpg (%0)" :: "r"(cr2) : "memory");
                            return;
                        }
                    } else {
                        *pte_ptr = (*pte_ptr | (1ULL << 1)) & ~(1ULL << 9);
                        __asm__ volatile("invlpg (%0)" :: "r"(cr2) : "memory");
                        return;
                    }
                }
            }

            // on-demand user stack growth
            if (is_user && task && task->ta_vm_map &&
                cr2 >= 0x700000000000ULL && cr2 < 0x800000000000ULL) {
                extern xiu_paddr_t pmm_alloc_page(void);
                extern u64 pmap_map_user_page(u64 target_pml4_phys, u64 vaddr, u64 paddr, u32 flags);
                u64 page_vaddr = cr2 & ~0xFFFULL;
                u64 new_phys = pmm_alloc_page();
                if (new_phys && new_phys != (u64)-1) {
                    void *hhdm = (void *)(new_phys + g_hhdm_base);
                    __builtin_memset(hhdm, 0, 4096);
                    pmap_map_user_page((u64)task->ta_vm_map, page_vaddr, new_phys, 0x01 | 0x02 | 0x04);
                    return;
                }
            }

            // user mode invalid access
            if (is_user && proc && proc->p_pid > 1) {
                kprintf("\n[FAULT] Process '%s' (PID %u) Segmentation Fault (#PF)\n",
                        proc->p_comm, proc->p_pid);
                kprintf("        RIP=0x%llx CR2=0x%llx RSP=0x%llx Error=0x%llx\n",
                        (unsigned long long)frame->rip, (unsigned long long)cr2,
                        (unsigned long long)frame->rsp, (unsigned long long)frame->err_code);
                extern void sys_exit_direct(u64 code);
                sys_exit_direct(139);
                return;
            }

            // kernel Mode Page Fault -> Kernel Panic
            kprintf("[KERNEL EXCEPTION] Page Fault (#PF) at RIP=%p CR2=%p Error=0x%llx\n",
                    (void*)frame->rip, (void*)cr2, frame->err_code);
            kprintf("                   RSP=%p RBP=%p RAX=%p\n",
                    (void*)frame->rsp, (void*)frame->rbp, (void*)frame->rax);
            xiu_panic("Page Fault in Kernel Mode: RIP=%p CR2=%p Error=0x%llx\n",
                      (void*)frame->rip, (void*)cr2, frame->err_code);
        }

        if (is_user && proc && proc->p_pid > 1) {
            kprintf("\n[FAULT] Process '%s' (PID %u) CPU Exception #%llu at RIP=0x%llx\n",
                    proc->p_comm, proc->p_pid, (unsigned long long)frame->int_no,
                    (unsigned long long)frame->rip);
            extern void sys_exit_direct(u64 code);
            sys_exit_direct(128 + frame->int_no);
            return;
        }

        kprintf("[KERNEL EXCEPTION] CPU Exception #%llu at RIP=%p Error=0x%llx\n",
                frame->int_no, (void*)frame->rip, frame->err_code);
        xiu_panic("CPU EXCEPTION %llu at RIP=%p (Error Code: 0x%llx)\n",
                  frame->int_no, (void*)frame->rip, frame->err_code);
    }
}
