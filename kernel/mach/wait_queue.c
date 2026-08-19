/* =============================================================================
 * XIU Operating System — Wait Queues Implementation
 * kernel/mach/wait_queue.c
 * =============================================================================
 */

#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/wait_queue.h>

extern void scheduler_yield(void);
extern void thread_wake(xiu_thread_t *thread);

void wait_queue_init(wait_queue_t *wq) {
  wq->head = nullptr;
  wq->tail = nullptr;
}

xiu_error_t wait_queue_sleep(wait_queue_t *wq, spinlock_t *lock) {
  xiu_thread_t *curr = current_thread();
  XIU_ASSERT(curr != nullptr);

  // mark as waiting
  curr->th_state = THREAD_STATE_WAITING;
  curr->th_wait_next = nullptr;

  // add to queue
  if (wq->tail) {
    wq->tail->th_wait_next = curr;
  } else {
    wq->head = curr;
  }
  wq->tail = curr;

  // unlock and yield
  if (lock) {
    spinlock_unlock(lock);
  }

  scheduler_yield();

  return curr->th_wait_result;
}

xiu_error_t wait_queue_sleep_irqrestore(wait_queue_t *wq, spinlock_t *lock,
                                        irq_flags_t flags) {
  xiu_thread_t *curr = current_thread();
  XIU_ASSERT(curr != nullptr);

  curr->th_state = THREAD_STATE_WAITING;
  curr->th_wait_next = nullptr;

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

  scheduler_yield();

  return curr->th_wait_result;
}

void wait_queue_wakeup_one(wait_queue_t *wq) {
  if (!wq->head)
    return;

  xiu_thread_t *th = wq->head;
  wq->head = th->th_wait_next;
  if (!wq->head) {
    wq->tail = nullptr;
  }
  th->th_wait_next = nullptr;

  th->th_state = THREAD_STATE_READY;
  th->th_wait_result = XIU_SUCCESS;

  thread_wake(th);
}
