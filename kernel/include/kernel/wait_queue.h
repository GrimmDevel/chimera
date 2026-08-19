/* =============================================================================
 * XIU Operating System — Wait Queues
 * kernel/include/kernel/wait_queue.h
 * ============================================================================= */

#pragma once
#ifndef XIU_WAIT_QUEUE_H
#define XIU_WAIT_QUEUE_H

#include <kernel/xiu_types.h>
#include <kernel/spinlock.h>

struct xiu_thread;

typedef struct wait_queue {
    struct xiu_thread *head;
    struct xiu_thread *tail;
} wait_queue_t;

#define WAIT_QUEUE_INIT { .head = nullptr, .tail = nullptr }

void wait_queue_init(wait_queue_t *wq);

/*
 * wait_queue_sleep — Block the current thread on a wait queue.
 *   The provided lock must be held on entry and will be released before yielding.
 */
xiu_error_t wait_queue_sleep(wait_queue_t *wq, spinlock_t *lock);

/*
 * wait_queue_sleep_irqrestore — Block like wait_queue_sleep, but release a
 *   lock acquired with spinlock_lock_irqsave and restore the saved IRQ flags
 *   before yielding.
 */
xiu_error_t wait_queue_sleep_irqrestore(wait_queue_t *wq, spinlock_t *lock,
                                        irq_flags_t flags);

/*
 * wait_queue_wakeup_one — Wake the first thread in the queue.
 */
void wait_queue_wakeup_one(wait_queue_t *wq);

#endif /* XIU_WAIT_QUEUE_H */
