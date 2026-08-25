// kernel subsystem stubs
#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/xiu_types.h>
#include <stdarg.h>

extern xiu_thread_t *current_thread(void);
extern void task_switch_to_user(u64 entry, u64 stack);
extern void context_switch(void **old_sp, void *new_sp, u64 new_cr3, void *old_fp, void *new_fp);

#include <arch/x86_64/msr.h>

extern void x86_64_syscall_entry(void);

void cpu_init_syscall(void) {
  // enable syscall/sysret
  u64 efer = rdmsr(MSR_EFER);
  wrmsr(MSR_EFER, efer | 1);

  // setup star segments
  wrmsr(MSR_STAR, 0x0010000800000000ULL);

  // setup lstar entry point
  wrmsr(MSR_LSTAR, (uptr)x86_64_syscall_entry);

  // setup fmask (disable interrupts on syscall entry)
  wrmsr(MSR_FMASK, 0x200);
}

void xiu_assert_fail(const char *expr, const char *file, u32 line,
                     const char *func) {
  xiu_panic("ASSERTION FAILED: %s\nFile: %s, Line: %u, Func: %s\n", expr, file,
            line, func);
}

XIU_NORETURN void xiu_panic(const char *fmt, ...) {
  __asm__ volatile("cli");
  serial_puts("\n!!! KERNEL PANIK AHTUNG PENGUIN HERE!!!\n");

  va_list args;
  va_start(args, fmt);
  kvprintf(fmt, args);
  va_end(args);

  serial_puts("\nSystem Halted.\n");
  for (;;) {
    __asm__ volatile("hlt");
  }
}
