/* =============================================================================
 * Chimera Operating System — Wait Queues Implementation
 * kernel/mach/wait_queue.c
 * =============================================================================
 */

#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/wait_queue.h>

extern void scheduler_yield(void);
extern void thread_wake(chimera_thread_t *thread);

void wait_queue_init(wait_queue_t *wq) {
  spinlock_init(&wq->wq_lock);
  wq->head = nullptr;
  wq->tail = nullptr;
}

chimera_error_t wait_queue_sleep(wait_queue_t *wq, spinlock_t *lock) {
  chimera_thread_t *curr = current_thread();
  CHIMERA_ASSERT(curr != nullptr);

  irq_flags_t wq_flags = spinlock_lock_irqsave(&wq->wq_lock);

  curr->th_state = THREAD_STATE_WAITING;
  curr->th_wait_next = nullptr;
  curr->th_wait_result = CHIMERA_SUCCESS;

  if (wq->tail) {
    wq->tail->th_wait_next = curr;
  } else {
    wq->head = curr;
  }
  wq->tail = curr;

  if (lock) {
    spinlock_unlock(lock);
  }

  spinlock_unlock_irqrestore(&wq->wq_lock, wq_flags);

  scheduler_yield();

  return curr->th_wait_result;
}

chimera_error_t wait_queue_sleep_irqrestore(wait_queue_t *wq, spinlock_t *lock,
                                        irq_flags_t flags) {
  chimera_thread_t *curr = current_thread();
  CHIMERA_ASSERT(curr != nullptr);

  irq_flags_t wq_flags = spinlock_lock_irqsave(&wq->wq_lock);

  curr->th_state = THREAD_STATE_WAITING;
  curr->th_wait_next = nullptr;
  curr->th_wait_result = CHIMERA_SUCCESS;

  if (wq->tail) {
    wq->tail->th_wait_next = curr;
  } else {
    wq->head = curr;
  }
  wq->tail = curr;

  if (lock) {
    spinlock_unlock_irqrestore(lock, flags);
  } else {
    irq_restore(flags);
  }

  spinlock_unlock_irqrestore(&wq->wq_lock, wq_flags);

  scheduler_yield();

  return curr->th_wait_result;
}

void wait_queue_wakeup_one(wait_queue_t *wq) {
  if (!wq) return;
  irq_flags_t wq_flags = spinlock_lock_irqsave(&wq->wq_lock);

  if (!wq->head) {
    spinlock_unlock_irqrestore(&wq->wq_lock, wq_flags);
    return;
  }

  chimera_thread_t *th = wq->head;
  wq->head = th->th_wait_next;
  if (!wq->head) {
    wq->tail = nullptr;
  }
  th->th_wait_next = nullptr;
  th->th_state = THREAD_STATE_READY;
  th->th_wait_result = CHIMERA_SUCCESS;

  spinlock_unlock_irqrestore(&wq->wq_lock, wq_flags);

  thread_wake(th);
}

void wait_queue_wakeup_all(wait_queue_t *wq) {
  if (!wq) return;
  irq_flags_t wq_flags = spinlock_lock_irqsave(&wq->wq_lock);

  chimera_thread_t *list = wq->head;
  wq->head = nullptr;
  wq->tail = nullptr;

  spinlock_unlock_irqrestore(&wq->wq_lock, wq_flags);

  while (list) {
    chimera_thread_t *next = list->th_wait_next;
    list->th_wait_next = nullptr;
    list->th_state = THREAD_STATE_READY;
    list->th_wait_result = CHIMERA_SUCCESS;
    thread_wake(list);
    list = next;
  }
}
