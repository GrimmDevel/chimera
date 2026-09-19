#include "idt.h"
#include <kernel/io.h>
#include <kernel/lapic.h>
#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/smp.h>

extern void kprintf(const char *fmt, ...);

volatile u64 g_system_ticks = 0;
static _Atomic(u64) s_monotonic_ticks = 0;
static irq_stats_t s_irq_stats;

u64 timer_get_monotonic_ticks(void) {
  return atomic_load_explicit(&s_monotonic_ticks, memory_order_acquire);
}

void smp_get_irq_stats(irq_stats_t *out) {
  if (!out) return;
  out->count_pit = __atomic_load_n(&s_irq_stats.count_pit, __ATOMIC_RELAXED);
  out->count_lapic_timer = __atomic_load_n(&s_irq_stats.count_lapic_timer, __ATOMIC_RELAXED);
  out->count_ipi_sched = __atomic_load_n(&s_irq_stats.count_ipi_sched, __ATOMIC_RELAXED);
  out->count_ipi_tlb = __atomic_load_n(&s_irq_stats.count_ipi_tlb, __ATOMIC_RELAXED);
  out->count_e1000_msi = __atomic_load_n(&s_irq_stats.count_e1000_msi, __ATOMIC_RELAXED);
  out->count_spurious = __atomic_load_n(&s_irq_stats.count_spurious, __ATOMIC_RELAXED);
}

void smp_dump_irq_stats(void) {
  irq_stats_t s;
  smp_get_irq_stats(&s);
  kprintf("  [  OK  ]  IRQ stats: PIT=%llu, LAPIC-timer=%llu, IPI-sched=%llu, IPI-TLB=%llu, e1000=%llu, Spurious=%llu\n",
          (unsigned long long)s.count_pit,
          (unsigned long long)s.count_lapic_timer,
          (unsigned long long)s.count_ipi_sched,
          (unsigned long long)s.count_ipi_tlb,
          (unsigned long long)s.count_e1000_msi,
          (unsigned long long)s.count_spurious);
}

static inline u64 rdtsc(void) {
  u32 low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  return ((u64)high << 32) | low;
}

static u64 s_boot_tsc = 0;
static u64 s_tsc_hz = 2400000000ULL; // default 2.4 GHz

static void tsc_init(void) {
  s_boot_tsc = rdtsc();
  
  // calibrate via PIT Channel 2 (gate port 0x61, counter port 0x42)
  outb(0x61, (inb(0x61) & ~0x02) | 0x01); // enable gate
  outb(0x43, 0xB0); // channel 2, lobyte/hibyte, mode 0 (one-shot)
  
  // 50ms interval: 1193182 * 0.05 = 59659 ticks
  u16 count = 59659;
  outb(0x42, count & 0xFF);
  outb(0x42, (count >> 8) & 0xFF);

  u8 v = inb(0x61) & 0xFE;
  outb(0x61, v);
  outb(0x61, v | 0x01); // start PIT2 countdown

  u64 t0 = rdtsc();
  u32 spin = 10000000;
  while ((inb(0x61) & 0x20) == 0 && --spin > 0) {
    __asm__ volatile("pause");
  }
  u64 t1 = rdtsc();

  if (t1 > t0 + 1000000) {
    s_tsc_hz = (t1 - t0) * 20; // 50ms * 20 = 1 sec
  }
}

u64 timer_get_uptime_ns(void) {
  u64 now = rdtsc();
  if (now <= s_boot_tsc || s_tsc_hz == 0) return 0;
  return (now - s_boot_tsc) * 1000000000ULL / s_tsc_hz;
}

u64 timer_get_uptime_ms(void) {
  u64 now = rdtsc();
  if (now <= s_boot_tsc || s_tsc_hz == 0) return 0;
  return (now - s_boot_tsc) * 1000ULL / s_tsc_hz;
}

u64 timer_get_uptime_seconds(void) {
  u64 now = rdtsc();
  if (now <= s_boot_tsc || s_tsc_hz == 0) return 0;
  return (now - s_boot_tsc) / s_tsc_hz;
}

// calibrated TSC frequency (0 = not calibrated yet); drivers use this to
// build time-based device timeouts instead of unbounded spin counts
u64 timer_tsc_hz(void) { return s_tsc_hz; }

static struct idt_entry idt[256];
static struct idtr idtr;

extern void *interrupt_handlers[];

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

  // configure IST for critical exceptions
  idt[8].ist = 1; // Double Fault (#DF) uses IST1
  idt[2].ist = 2; // NMI uses IST2

  idtr.limit = sizeof(idt) - 1;
  idtr.base = (u64)idt;

  __asm__ volatile("lidt %0" : : "m"(idtr));

  pit_init();
  pic_remap();
  tsc_init();

  kprintf("  [  OK  ]  Architecture IDT, PIC & TSC Timer\n");
}

void idt_reload(void) { __asm__ volatile("lidt %0" : : "m"(idtr)); }

// c-level interrupt dispatcher
struct interrupt_frame {
  u64 r15, r14, r13, r12, r11, r10, r9, r8;
  u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
  u64 int_no, err_code;
  u64 rip, cs, rflags, rsp, ss;
};

void chimerakit_hid_irq_handler(void);
extern void scheduler_yield(void);
extern void lapic_eoi(void);

static u64 read_cr2(void) {
  u64 cr2;
  __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
  return cr2;
}

void interrupt_handler(struct interrupt_frame *frame) {
  if (frame->int_no == VECTOR_IPI_SCHED) {
    __atomic_fetch_add(&s_irq_stats.count_ipi_sched, 1, __ATOMIC_RELAXED);
    lapic_eoi();
    u32 cpu_id = smp_current_cpu_id();
    if (cpu_id < CHIMERA_MAX_CPUS) {
      atomic_store_explicit(&g_cpu_data[cpu_id].cpu_need_resched, 0, memory_order_release);
    }
    scheduler_yield();
    return;
  } else if (frame->int_no == VECTOR_IPI_TLB) {
    __atomic_fetch_add(&s_irq_stats.count_ipi_tlb, 1, __ATOMIC_RELAXED);
    extern void smp_tlb_ipi_handler(void);
    smp_tlb_ipi_handler();
    lapic_eoi();
    return;
  } else if (frame->int_no == VECTOR_SPURIOUS) {
    __atomic_fetch_add(&s_irq_stats.count_spurious, 1, __ATOMIC_RELAXED);
    // Spurious interrupts do not receive EOI
    return;
  } else if (frame->int_no == VECTOR_E1000_MSI) {
    __atomic_fetch_add(&s_irq_stats.count_e1000_msi, 1, __ATOMIC_RELAXED);
    extern void e1000_isr(void);
    e1000_isr();
    lapic_eoi();
    return;
  } else if (frame->int_no == VECTOR_LAPIC_TIMER) {
    __atomic_fetch_add(&s_irq_stats.count_lapic_timer, 1, __ATOMIC_RELAXED);
    // Strict EOI separation: LAPIC timer receives ONLY lapic_eoi, never PIC EOI
    lapic_eoi();
    u32 cpu_id = smp_current_cpu_id();
    if (cpu_id < CHIMERA_MAX_CPUS && g_cpu_data[cpu_id].cpu_is_active) {
      chimera_thread_t *th = current_thread();
      if (th && th != g_cpu_data[cpu_id].cpu_idle_thread && th->th_state == THREAD_STATE_RUNNING) {
        th->th_cpu_usage++;
        if (th->th_sched_priority > th->th_base_priority / 2) {
          th->th_sched_priority--;
        }
        th->th_priority = th->th_sched_priority;
        atomic_store_explicit(&g_cpu_data[cpu_id].cpu_need_resched, 1, memory_order_release);
      }
      if (atomic_load_explicit(&g_cpu_data[cpu_id].cpu_need_resched, memory_order_acquire)) {
        atomic_store_explicit(&g_cpu_data[cpu_id].cpu_need_resched, 0, memory_order_release);
        scheduler_yield();
      }
    }
    return;
  } else if (frame->int_no == VECTOR_PIT_TIMER) {
    __atomic_fetch_add(&s_irq_stats.count_pit, 1, __ATOMIC_RELAXED);

    // Single-writer monotonic clock: BSP only
    atomic_fetch_add_explicit(&s_monotonic_ticks, 1, memory_order_release);
    g_system_ticks = atomic_load_explicit(&s_monotonic_ticks, memory_order_relaxed);

    // release timer-based sleepers before anything else
    extern void timer_wake_sleepers(void);
    timer_wake_sleepers();


    // Broadcast IPI storm eliminated: per-CPU need_resched replaces blind IPI broadcasts
    extern void chimerakit_hid_poll(void);
    chimerakit_hid_poll();

    // Legacy PIC EOI
    outb(0x20, 0x20);
    lapic_eoi();

    // BSP local timeslice accounting
    chimera_thread_t *th = current_thread();
    if (th && th != g_cpu_data[0].cpu_idle_thread && th->th_state == THREAD_STATE_RUNNING) {
      th->th_cpu_usage++;
      if (th->th_sched_priority > th->th_base_priority / 2) {
        th->th_sched_priority--;
      }
      th->th_priority = th->th_sched_priority;
      atomic_store_explicit(&g_cpu_data[0].cpu_need_resched, 1, memory_order_release);
    }
    if (atomic_load_explicit(&g_cpu_data[0].cpu_need_resched, memory_order_acquire)) {
      atomic_store_explicit(&g_cpu_data[0].cpu_need_resched, 0, memory_order_release);
      scheduler_yield();
    }
  } else if (frame->int_no == VECTOR_MOUSE) {
    chimerakit_hid_irq_handler();
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
    lapic_eoi();
  } else if (frame->int_no == VECTOR_KEYBOARD) {
    chimerakit_hid_irq_handler();
    outb(0x20, 0x20);
    lapic_eoi();
  } else if (frame->int_no >= 32 && frame->int_no <= 47) {
    if (frame->int_no >= 40)
      outb(0xA0, 0x20);
    outb(0x20, 0x20);
    lapic_eoi();
  } else if (frame->int_no < 32) {
    bool is_user = ((frame->cs & 3) == 3) || (frame->rip < 0x0000800000000000ULL);
    chimera_task_t *task = current_task();
    chimera_proc_t *proc = task ? task->ta_proc : nullptr;

    if (frame->int_no == 14) {
      u64 cr2 = read_cr2();

      // copy-on-write fault
      if (is_user && task && task->ta_vm_map && (frame->err_code & 2)) {
        extern bool pmap_handle_cow_fault(u64 pml4_phys, u64 fault_va);
        if (pmap_handle_cow_fault((u64)task->ta_vm_map, cr2)) {
          return;
        }
      }

      // on-demand user stack growth within legitimate user stack boundaries [USER_STACK_MIN, USER_STACK_MAX]
      #define USER_STACK_MIN 0x00007FF000000000ULL
      #define USER_STACK_MAX 0x00007FFFFFFFFFFFULL
      if (is_user && task && task->ta_vm_map && cr2 >= USER_STACK_MIN && cr2 <= USER_STACK_MAX) {
        extern chimera_paddr_t pmm_alloc_page(void);
        extern u64 pmap_map_user_page(u64 target_pml4_phys, u64 vaddr,
                                      u64 paddr, u64 flags);
        u64 page_vaddr = cr2 & ~0xFFFULL;
        u64 new_phys = pmm_alloc_page();
        if (new_phys && new_phys != (u64)-1) {
          void *hhdm = (void *)(new_phys + g_hhdm_base);
          __builtin_memset(hhdm, 0, 4096);
          // stack pages are data: W^X, never executable
          pmap_map_user_page((u64)task->ta_vm_map, page_vaddr, new_phys,
                             0x01 | 0x02 | 0x04 | (1ULL << 63));
          __asm__ volatile("invlpg (%0)" ::"r"(cr2) : "memory");
          return;
        }
      }

      // user mode invalid access
      if (is_user) {
        bool valid_proc = (proc && proc->p_signature == CHIMERA_PROC_MAGIC);
        if (valid_proc && proc->p_pid > 1) {
          kprintf("\n[FAULT] Process '%s' (PID %u) Segmentation Fault (#PF)\n",
                  proc->p_comm, proc->p_pid);
          kprintf("        RIP=0x%llx CR2=0x%llx RSP=0x%llx Error=0x%llx\n",
                  (unsigned long long)frame->rip, (unsigned long long)cr2,
                  (unsigned long long)frame->rsp,
                  (unsigned long long)frame->err_code);
        }
        extern void sys_exit_direct(u64 code);
        sys_exit_direct(139);
        return;
      }

      // kernel Mode Page Fault -> Kernel Panic
      kprintf(
          "[KERNEL EXCEPTION] Page Fault (#PF) at RIP=%p CR2=%p Error=0x%llx\n",
          (void *)frame->rip, (void *)cr2, frame->err_code);
      kprintf("                   RSP=%p RBP=%p RAX=%p\n", (void *)frame->rsp,
              (void *)frame->rbp, (void *)frame->rax);
      chimera_panic("Page Fault in Kernel Mode: RIP=%p CR2=%p Error=0x%llx\n",
                (void *)frame->rip, (void *)cr2, frame->err_code);
    }

    if (is_user) {
      bool valid_proc = (proc && proc->p_signature == CHIMERA_PROC_MAGIC);
      if (valid_proc && proc->p_pid > 1) {
        kprintf(
            "\n[FAULT] Process '%s' (PID %u) CPU Exception #%llu at RIP=0x%llx\n",
            proc->p_comm, proc->p_pid, (unsigned long long)frame->int_no,
            (unsigned long long)frame->rip);
      }
      extern void sys_exit_direct(u64 code);
      sys_exit_direct(128 + frame->int_no);
      return;
    }

    kprintf("[KERNEL EXCEPTION] CPU Exception #%llu at RIP=%p Error=0x%llx\n",
            frame->int_no, (void *)frame->rip, frame->err_code);
    chimera_panic("CPU EXCEPTION %llu at RIP=%p (Error Code: 0x%llx)\n",
              frame->int_no, (void *)frame->rip, frame->err_code);
  }
}
