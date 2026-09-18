// kernel subsystem stubs
#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/chimera_types.h>
#include <stdarg.h>

extern chimera_thread_t *current_thread(void);
extern void task_switch_to_user(u64 entry, u64 stack);
extern void context_switch(void **old_sp, void *new_sp, u64 new_cr3, void *old_fp, void *new_fp, void *old_running_cpu);

#include <arch/x86_64/msr.h>

extern void x86_64_syscall_entry(void);

void cpu_init_syscall(void) {
  // enable syscall/sysret (SCE) and NX page-bit enforcement (NXE).
  // Without NXE the CPU treats PTE bit 63 as a reserved-bit violation,
  // so any mprotect() that hides PROT_EXEC would fault instead of working.
  u64 efer = rdmsr(MSR_EFER);
  wrmsr(MSR_EFER, efer | 1 | (1ULL << 11));

  // setup star segments
  wrmsr(MSR_STAR, 0x0010000800000000ULL);

  // setup lstar entry point
  wrmsr(MSR_LSTAR, (uptr)x86_64_syscall_entry);

  // setup fmask (disable interrupts on syscall entry)
  wrmsr(MSR_FMASK, 0x200);
}

void chimera_assert_fail(const char *expr, const char *file, u32 line,
                     const char *func) {
  chimera_panic("ASSERTION FAILED: %s\nFile: %s, Line: %u, Func: %s\n", expr, file,
            line, func);
}

CHIMERA_NORETURN void chimera_panic(const char *fmt, ...) {
  __asm__ volatile("cli");
  serial_puts("\n!!! KERNEL PANIC: CHIMERA HALTED !!!\n");

  va_list args;
  va_start(args, fmt);
  kvprintf(fmt, args);
  va_end(args);

  // boot context: which CPU, how long the system ran before failing
  extern u32 smp_current_cpu_id(void);
  extern u64 timer_get_uptime_ms(void);
  kprintf("cpu=%u uptime=%llu ms\n", smp_current_cpu_id(),
          (unsigned long long)timer_get_uptime_ms());

  // control registers: CR2 holds the faulting address for #PF panics
  u64 cr0 = 0, cr2 = 0, cr3 = 0, cr4 = 0;
  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
  __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
  kprintf("cr0=0x%016llx cr2=0x%016llx\n",
           (unsigned long long)cr0, (unsigned long long)cr2);
  kprintf("cr3=0x%016llx cr4=0x%016llx\n",
           (unsigned long long)cr3, (unsigned long long)cr4);

  // best-effort RBP backtrace (the kernel builds with
  // -fno-omit-frame-pointer). Every frame is validated against the kernel
  // image range before it is trusted.
  u64 *rbp = nullptr;
  __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
  const u64 kimg_lo = 0xffffff8000000000ULL;
  const u64 kimg_hi = kimg_lo + 0x04000000ULL; // 64 MiB window
  kprintf("backtrace (rbp=%p):\n", (void *)rbp);
  for (int i = 0; i < 16 && rbp; i++) {
    u64 rip = rbp[1];
    if (rip < kimg_lo || rip >= kimg_hi)
      break;
    kprintf("  #%d: 0x%016llx\n", i, (unsigned long long)rip);
    u64 *next = (u64 *)rbp[0];
    if (next <= rbp || (u64)next < kimg_lo || (u64)next >= kimg_hi)
      break;
    rbp = next;
  }

  serial_puts("\nSystem Halted.\n");
  for (;;) {
    __asm__ volatile("hlt");
  }
}
