// kernel panic handler
#pragma once
#ifndef CHIMERA_PANIC_H
#define CHIMERA_PANIC_H

#include <kernel/chimera_types.h>
#include <stdarg.h>

#define CHIMERA_PANIC_LOG_SIZE  (64 * 1024)

typedef u32 panic_flags_t;
#define PANIC_REBOOT_ON_PANIC   (1u << 0)
#define PANIC_DUMP_REGISTERS    (1u << 1)
#define PANIC_DUMP_BACKTRACE    (1u << 2)
#define PANIC_DUMP_VMSTATE      (1u << 3)
#define PANIC_DUMP_IPC          (1u << 4)

extern panic_flags_t chimera_panic_flags;

typedef struct panic_cpu_state {
    u64 rax, rbx, rcx, rdx, rsi, rdi, rbp;
    u64 r8,  r9,  r10, r11, r12, r13, r14, r15;
    u64 rip, rflags, rsp, ss, cs;
    u64 cr0, cr2, cr3, cr4;
    u64 error_code;
    u32 exception_vector;
    u32 cpu_id;
} panic_cpu_state_t;

#ifdef __cplusplus
extern "C" {
#endif

CHIMERA_NORETURN CHIMERA_COLD
void chimera_panic(const char *fmt, ...);

CHIMERA_NORETURN CHIMERA_COLD
void chimera_panic_with_context(const panic_cpu_state_t *ctx, const char *fmt, ...);

CHIMERA_NORETURN CHIMERA_COLD
void chimera_assert_fail(const char *expr, const char *file, u32 line, const char *func);

void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list args);

#ifdef CHIMERA_VERBOSE
#define dprintf(fmt, ...) kprintf(fmt, ##__VA_ARGS__)
#else
#define dprintf(fmt, ...) do { (void)(fmt); } while (0)
#endif

void serial_putc(char c);
void serial_puts(const char *s);

#ifdef __cplusplus
}
#endif

#define CHIMERA_ASSERT(expr)                                                    \
    do {                                                                    \
        if (CHIMERA_UNLIKELY(!(expr))) {                                        \
            chimera_assert_fail(#expr, __FILE__, __LINE__, __func__);           \
        }                                                                   \
    } while (0)

#define CHIMERA_ASSERT_MSG(expr, ...)                                           \
    do {                                                                    \
        if (CHIMERA_UNLIKELY(!(expr))) {                                        \
            chimera_panic("ASSERT(" #expr ") failed at %s:%d\n  " __VA_ARGS__, \
                      __FILE__, __LINE__);                                  \
        }                                                                   \
    } while (0)

#define CHIMERA_PANIC(...)   chimera_panic(__VA_ARGS__)

#endif
