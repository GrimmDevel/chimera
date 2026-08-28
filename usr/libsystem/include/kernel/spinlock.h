// spinlocks and sync primitives
#pragma once
#ifndef CHIMERA_SPINLOCK_H
#define CHIMERA_SPINLOCK_H

#include <kernel/chimera_types.h>

typedef struct CHIMERA_ALIGNED(8) spinlock {
    _Atomic(u32)    sl_next;
    _Atomic(u32)    sl_serving;
#if defined(NDEBUG)
    u8              _pad[56];
#else
    const char     *sl_owner;
    u64             sl_acquire_time;
    u8              _pad[40];
#endif
} spinlock_t;

#define SPINLOCK_INIT   { .sl_next = 0, .sl_serving = 0 }

#if defined(CHIMERA_ARCH_x86_64)
#  define cpu_relax()  __asm__ volatile("pause" ::: "memory")
#elif defined(CHIMERA_ARCH_arm64)
#  define cpu_relax()  __asm__ volatile("yield" ::: "memory")
#else
#  define cpu_relax()  CHIMERA_BARRIER()
#endif

CHIMERA_ALWAYS_INLINE void spinlock_init(spinlock_t *lock) {
    atomic_store_explicit(&lock->sl_next,    0, memory_order_relaxed);
    atomic_store_explicit(&lock->sl_serving, 0, memory_order_relaxed);
}

CHIMERA_ALWAYS_INLINE void spinlock_lock(spinlock_t *lock) {
    u32 my_ticket = atomic_fetch_add_explicit(&lock->sl_next, 1, memory_order_relaxed);
    while (atomic_load_explicit(&lock->sl_serving, memory_order_acquire) != my_ticket) {
        cpu_relax();
    }
}

CHIMERA_ALWAYS_INLINE bool spinlock_trylock(spinlock_t *lock) {
    u32 serving = atomic_load_explicit(&lock->sl_serving, memory_order_relaxed);
    u32 next    = atomic_load_explicit(&lock->sl_next,    memory_order_relaxed);
    if (serving != next) return false;
    return atomic_compare_exchange_strong_explicit(
        &lock->sl_next, &next, next + 1,
        memory_order_acquire, memory_order_relaxed);
}

CHIMERA_ALWAYS_INLINE void spinlock_unlock(spinlock_t *lock) {
    u32 now = atomic_load_explicit(&lock->sl_serving, memory_order_relaxed);
    atomic_store_explicit(&lock->sl_serving, now + 1, memory_order_release);
}

#define SPINLOCK_HELD(lock)                         \
    for (int _sl_once_ = (spinlock_lock(lock), 1);  \
         _sl_once_;                                  \
         _sl_once_ = (spinlock_unlock(lock), 0))

typedef struct CHIMERA_ALIGNED(16) rwspinlock {
    _Atomic(s32)    rw_state;
    _Atomic(u32)    rw_pending_writers;
} rwspinlock_t;

#define RWSPINLOCK_INIT   { 0, 0 }

CHIMERA_ALWAYS_INLINE void rwspinlock_init(rwspinlock_t *rw) {
    atomic_store(&rw->rw_state, 0);
    atomic_store(&rw->rw_pending_writers, 0);
}

CHIMERA_ALWAYS_INLINE void rwspinlock_read_lock(rwspinlock_t *rw) {
    for (;;) {
        while (atomic_load_explicit(&rw->rw_pending_writers, memory_order_relaxed) > 0 ||
               atomic_load_explicit(&rw->rw_state, memory_order_relaxed) < 0) {
            cpu_relax();
        }
        s32 old = atomic_fetch_add_explicit(&rw->rw_state, 1, memory_order_acquire);
        if (old >= 0) return;
        atomic_fetch_sub_explicit(&rw->rw_state, 1, memory_order_relaxed);
    }
}

CHIMERA_ALWAYS_INLINE void rwspinlock_read_unlock(rwspinlock_t *rw) {
    atomic_fetch_sub_explicit(&rw->rw_state, 1, memory_order_release);
}

CHIMERA_ALWAYS_INLINE void rwspinlock_write_lock(rwspinlock_t *rw) {
    atomic_fetch_add_explicit(&rw->rw_pending_writers, 1, memory_order_relaxed);
    s32 expected = 0;
    while (!atomic_compare_exchange_weak_explicit(&rw->rw_state,
                                                  &expected, -1,
                                                  memory_order_acquire,
                                                  memory_order_relaxed)) {
        expected = 0;
        cpu_relax();
    }
    atomic_fetch_sub_explicit(&rw->rw_pending_writers, 1, memory_order_relaxed);
}

CHIMERA_ALWAYS_INLINE void rwspinlock_write_unlock(rwspinlock_t *rw) {
    atomic_store_explicit(&rw->rw_state, 0, memory_order_release);
}

#if defined(CHIMERA_ARCH_x86_64)

typedef u64 irq_flags_t;

CHIMERA_ALWAYS_INLINE irq_flags_t irq_save(void) {
    irq_flags_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=rm"(flags) :: "memory");
    return flags;
}

CHIMERA_ALWAYS_INLINE void irq_restore(irq_flags_t flags) {
    __asm__ volatile("pushq %0; popfq" :: "rm"(flags) : "memory", "cc");
}

#elif defined(CHIMERA_ARCH_arm64)

typedef u64 irq_flags_t;

CHIMERA_ALWAYS_INLINE irq_flags_t irq_save(void) {
    irq_flags_t flags;
    __asm__ volatile("mrs %0, daif; msr daifset, #3" : "=r"(flags) :: "memory");
    return flags;
}

CHIMERA_ALWAYS_INLINE void irq_restore(irq_flags_t flags) {
    __asm__ volatile("msr daif, %0" :: "r"(flags) : "memory");
}

#else
typedef u64 irq_flags_t;
CHIMERA_ALWAYS_INLINE irq_flags_t irq_save(void)             { return 0; }
CHIMERA_ALWAYS_INLINE void        irq_restore(irq_flags_t f) { (void)f;  }
#endif

CHIMERA_ALWAYS_INLINE irq_flags_t spinlock_lock_irqsave(spinlock_t *lock) {
    irq_flags_t flags = irq_save();
    spinlock_lock(lock);
    return flags;
}

CHIMERA_ALWAYS_INLINE void spinlock_unlock_irqrestore(spinlock_t *lock,
                                                   irq_flags_t flags) {
    spinlock_unlock(lock);
    irq_restore(flags);
}

#define smp_mb()    atomic_thread_fence(memory_order_seq_cst)
#define smp_rmb()   atomic_thread_fence(memory_order_acquire)
#define smp_wmb()   atomic_thread_fence(memory_order_release)
#define smp_store_release(p, v) \
    atomic_store_explicit((p), (v), memory_order_release)
#define smp_load_acquire(p) \
    atomic_load_explicit((p), memory_order_acquire)

#endif
