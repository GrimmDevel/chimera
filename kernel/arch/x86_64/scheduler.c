// thread scheduler
#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/spinlock.h>
#include <kernel/smp.h>
#include <arch/x86_64/msr.h>

extern void kprintf(const char *fmt, ...);
extern void context_switch(void **old_sp, void *new_sp, u64 new_cr3, void *old_fp, void *new_fp, void *old_running_cpu);
extern u32 smp_current_cpu_id(void);
extern void *kalloc(usize size);
extern void kfree(void *ptr);

void scheduler_yield(void);

typedef struct cpu_runq {
  spinlock_t lock;
  chimera_thread_t *head; // priority-ordered linked list via th_next
  u32 count;
} cpu_runq_t;

static cpu_runq_t s_cpu_runq[CHIMERA_MAX_CPUS];
static chimera_thread_t s_cpu_idle_threads[CHIMERA_MAX_CPUS];

void idle_thread_entry(void) {
  for (;;) {
    __asm__ volatile("sti; hlt" ::: "memory");
    scheduler_yield();
  }
}

static void runq_enqueue_locked(cpu_runq_t *rq, chimera_thread_t *th) {
  chimera_thread_t **pp = &rq->head;
  while (*pp && (*pp)->th_priority >= th->th_priority) {
    pp = &(*pp)->th_next;
  }
  th->th_next = *pp;
  *pp = th;
  rq->count++;
}

static chimera_thread_t *runq_dequeue_locked(cpu_runq_t *rq) {
  chimera_thread_t *th = rq->head;
  if (th) {
    rq->head = th->th_next;
    th->th_next = nullptr;
    rq->count--;
  }
  return th;
}

static void runq_remove_locked(cpu_runq_t *rq, chimera_thread_t *th) {
  chimera_thread_t **pp = &rq->head;
  while (*pp) {
    if (*pp == th) {
      *pp = th->th_next;
      th->th_next = nullptr;
      if (rq->count > 0) rq->count--;
      return;
    }
    pp = &(*pp)->th_next;
  }
}

void idle_thread_init(u32 cpu_id) {
  if (cpu_id >= CHIMERA_MAX_CPUS) return;
  chimera_thread_t *idle = &s_cpu_idle_threads[cpu_id];
  __builtin_memset(idle, 0, sizeof(chimera_thread_t));
  idle->th_signature = CHIMERA_THREAD_MAGIC;
  idle->th_id = 0x1D1E0000ULL | cpu_id;
  idle->th_task = task_kernel;
  idle->th_state = THREAD_STATE_READY;
  idle->th_priority = 0;
  idle->th_base_priority = 0;
  idle->th_sched_priority = 0;
  idle->th_running_cpu = cpu_id;
  idle->th_assigned_cpu = cpu_id;
  idle->th_kernel_stack = g_cpu_data[cpu_id].cpu_kernel_stack;
  if (idle->th_kernel_stack) {
    idle->th_stack_base = (void *)((u64)idle->th_kernel_stack - 16384);
    idle->th_stack_size = 16384;

    u64 *sp = (u64 *)idle->th_kernel_stack;
    *(--sp) = (u64)idle_thread_entry; // RIP
    *(--sp) = 0x202;                  // RFLAGS (IF=1)
    *(--sp) = 0;                      // RBP
    *(--sp) = 0;                      // RBX
    *(--sp) = 0;                      // R12
    *(--sp) = 0;                      // R13
    *(--sp) = 0;                      // R14
    *(--sp) = 0;                      // R15
    idle->th_saved_sp = sp;
  }
  g_cpu_data[cpu_id].cpu_idle_thread = idle;
}

extern void task_switch_to_user(uptr entry, uptr stack);
extern void task_switch_to_user_frame(uptr entry, uptr stack, void *frame, u64 rax);
extern void tss_set_rsp0_cpu(u32 cpu_id, u64 rsp0);

static void thread_launcher(void);
static void fork_thread_launcher(void);

int thread_init_stack(chimera_thread_t *th, void *entry, void *stack) {
  extern chimera_paddr_t pmm_alloc_pages(usize count);
  extern u64 g_hhdm_base;

  if (!th->th_kernel_stack) {
    chimera_paddr_t paddr = pmm_alloc_pages(4);
    if (paddr == (chimera_paddr_t)-1 || paddr == 0) {
      // OOM is a caller-visible error, not a kernel panic
      kprintf("thread_init_stack: out of physical pages for kernel stack\n");
      return -1;
    }

    u8 *kstack_base = (u8 *)(g_hhdm_base + paddr);
    void *kstack_top = kstack_base + (4 * CHIMERA_PAGE_SIZE);

    th->th_stack_base = kstack_base;
    th->th_stack_size = 4 * CHIMERA_PAGE_SIZE;
    th->th_kernel_stack = kstack_top;
  }
  th->th_running_cpu = 0xFFFFFFFF;

  // initialize FPU/SSE state image
  th->th_fp_initialized = 1;
  __builtin_memset(th->th_fp_state, 0, sizeof(th->th_fp_state));

  u16 *fcw = (u16 *)&th->th_fp_state[0];
  *fcw = 0x037F;
  u32 *mxcsr = (u32 *)&th->th_fp_state[24];
  *mxcsr = 0x1F80;
  u32 *mxcsr_mask = (u32 *)&th->th_fp_state[28];
  *mxcsr_mask = 0xFFFF;
  {
    // xsave header (bytes 512..): declare every XCR0-enabled component
    // present. XRSTOR raises #GP when XSTATE_BV[0] = 0 while XCR0[0] = 1,
    // so an all-zero header is NOT a valid initial state.
    extern u32 g_fpu_mask_lo;
    extern u32 g_fpu_mask_hi;
    u64 *xstate_bv = (u64 *)&th->th_fp_state[512];
    *xstate_bv = ((u64)g_fpu_mask_hi << 32) | g_fpu_mask_lo;
    // xcomp_bv (bytes 528..) stays 0: non-compacted format
  }

  u64 *sp = (u64 *)th->th_kernel_stack;
  *(--sp) = (u64)thread_launcher;
  *(--sp) = 0x202; // rflags (IF=1)
  *(--sp) = 0; // rbp
  *(--sp) = 0; // rbx
  *(--sp) = 0; // r12
  *(--sp) = 0; // r13
  *(--sp) = 0; // r14
  *(--sp) = 0; // r15

  th->th_saved_sp = sp;
  th->th_context    = entry;
  th->th_user_stack = stack;
  return 0;
}

int thread_init_fork_stack(chimera_thread_t *th, void *entry, void *stack) {
  if (thread_init_stack(th, entry, stack) != 0)
    return -1;

  u64 *sp = (u64 *)th->th_saved_sp;
  sp[7] = (u64)fork_thread_launcher;
  return 0;
}

static void thread_launcher(void) {
  chimera_thread_t *th = current_thread();

  tss_set_rsp0_cpu(smp_current_cpu_id(), (u64)th->th_kernel_stack);

  u64 entry = (u64)th->th_context;
  u64 stack = (u64)th->th_user_stack;

  task_switch_to_user(entry, stack);
  CHIMERA_UNREACHABLE();
}

static void fork_thread_launcher(void) {
  chimera_thread_t *th = current_thread();
  tss_set_rsp0_cpu(smp_current_cpu_id(), (u64)th->th_kernel_stack);
  
  u64 fork_return_value = th->th_is_fork_child ? th->th_fork_return_value : 0;
  
  task_switch_to_user_frame((uptr)th->th_context, (uptr)th->th_user_stack,
                            th->th_user_frame, fork_return_value);
  CHIMERA_UNREACHABLE();
}

extern void cpu_init_syscall(void);

// ponytail: simple work stealing — if local run queue is empty, take one
// thread from the busiest active core. Known ceiling: linear scan of max 16
// cores under individual locks; upgrade path: hierarchical domain topology.
static chimera_thread_t *scheduler_steal_work(u32 my_cpu) {
  extern u32 smp_get_cpu_count(void);
  u32 total = smp_get_cpu_count();
  if (total <= 1) return nullptr;

  u32 best_target = 0xFFFFFFFF;
  u32 max_count = 1;

  for (u32 i = 0; i < total && i < CHIMERA_MAX_CPUS; i++) {
    if (i == my_cpu || !g_cpu_data[i].cpu_is_active) continue;
    u32 c = __atomic_load_n(&s_cpu_runq[i].count, __ATOMIC_RELAXED);
    if (c > max_count) {
      max_count = c;
      best_target = i;
    }
  }

  if (best_target != 0xFFFFFFFF) {
    cpu_runq_t *victim_rq = &s_cpu_runq[best_target];
    irq_flags_t vf = spinlock_lock_irqsave(&victim_rq->lock);
    chimera_thread_t *prev = nullptr;
    chimera_thread_t *curr = victim_rq->head;
    if (curr && victim_rq->count > 1) {
      while (curr->th_next) {
        prev = curr;
        curr = curr->th_next;
      }
      if (prev) {
        prev->th_next = nullptr;
      } else {
        victim_rq->head = nullptr;
      }
      victim_rq->count--;
      spinlock_unlock_irqrestore(&victim_rq->lock, vf);
      curr->th_next = nullptr;
      curr->th_assigned_cpu = my_cpu;
      return curr;
    }
    spinlock_unlock_irqrestore(&victim_rq->lock, vf);
  }
  return nullptr;
}

void scheduler_init(void) {
  cpu_init_syscall();
  extern u32 smp_get_cpu_count(void);
  u32 total = smp_get_cpu_count();
  if (total == 0) total = 1;

  for (u32 i = 0; i < CHIMERA_MAX_CPUS; i++) {
    spinlock_init(&s_cpu_runq[i].lock);
    s_cpu_runq[i].head = nullptr;
    s_cpu_runq[i].count = 0;
    if (i < total && g_cpu_data[i].cpu_kernel_stack) {
      idle_thread_init(i);
    }
  }

  wrmsr(MSR_GS_BASE, (u64)&cpu_local_bsp);
  wrmsr(MSR_KERNEL_GS_BASE, 0);
}

void scheduler_set_initial(chimera_thread_t *th) {
  if (!th) return;
  u32 assigned = th->th_assigned_cpu;
  if (assigned < CHIMERA_MAX_CPUS) {
    cpu_runq_t *rq = &s_cpu_runq[assigned];
    irq_flags_t f = spinlock_lock_irqsave(&rq->lock);
    runq_remove_locked(rq, th);
    spinlock_unlock_irqrestore(&rq->lock, f);
  }
  th->th_running_cpu = 0;
  th->th_assigned_cpu = 0;
  th->th_state = THREAD_STATE_RUNNING;
  cpu_local_bsp.cpu_current_thread = th;
}

int scheduler_add_thread(chimera_thread_t *th) {
  if (!th) return -1;

  if (th->th_base_priority == 0) th->th_base_priority = 32;
  th->th_sched_priority = th->th_base_priority;
  th->th_priority = th->th_sched_priority;
  th->th_cpu_usage = 0;
  th->th_running_cpu = 0xFFFFFFFF;
  th->th_state = THREAD_STATE_READY;

  // Pick target CPU: least loaded active CPU
  extern u32 smp_get_cpu_count(void);
  u32 total = smp_get_cpu_count();
  u32 target_cpu = 0;
  u32 min_count = 0xFFFFFFFF;

  for (u32 i = 0; i < total && i < CHIMERA_MAX_CPUS; i++) {
    if (!g_cpu_data[i].cpu_is_active && i != 0) continue;
    u32 c = __atomic_load_n(&s_cpu_runq[i].count, __ATOMIC_RELAXED);
    if (c < min_count) {
      min_count = c;
      target_cpu = i;
    }
  }

  th->th_assigned_cpu = target_cpu;

  cpu_runq_t *rq = &s_cpu_runq[target_cpu];
  irq_flags_t f = spinlock_lock_irqsave(&rq->lock);
  runq_enqueue_locked(rq, th);
  spinlock_unlock_irqrestore(&rq->lock, f);

  // Wake target CPU only if it is currently idle
  extern void smp_send_reschedule(u32 cpu_id);
  chimera_thread_t *target_curr = g_cpu_data[target_cpu].cpu_current_thread;
  if (!target_curr || target_curr == g_cpu_data[target_cpu].cpu_idle_thread) {
    smp_send_reschedule(target_cpu);
  }

  return 0;
}

void scheduler_remove_thread(chimera_thread_t *th) {
  if (!th) return;
  th->th_state = THREAD_STATE_HALTED;
  u32 target = th->th_assigned_cpu;
  if (target < CHIMERA_MAX_CPUS) {
    cpu_runq_t *rq = &s_cpu_runq[target];
    irq_flags_t f = spinlock_lock_irqsave(&rq->lock);
    runq_remove_locked(rq, th);
    spinlock_unlock_irqrestore(&rq->lock, f);
  }
}

chimera_thread_t *current_thread(void) {
  chimera_thread_t *th = nullptr;
  __asm__ volatile("mov %%gs:0, %0" : "=r"(th));
  return th;
}

void scheduler_yield(void) {
  u32 my_cpu = smp_current_cpu_id();
  chimera_thread_t *idle_th = g_cpu_data[my_cpu].cpu_idle_thread;
  chimera_thread_t *old_thread = current_thread();
  chimera_thread_t *new_thread = nullptr;

  cpu_runq_t *my_rq = &s_cpu_runq[my_cpu];
  irq_flags_t f = spinlock_lock_irqsave(&my_rq->lock);

  if (old_thread && old_thread != idle_th) {
    old_thread->th_cpu_usage++;
    if (old_thread->th_sched_priority > old_thread->th_base_priority / 2) {
      old_thread->th_sched_priority--;
    }
    old_thread->th_priority = old_thread->th_sched_priority;

    if (old_thread->th_state == THREAD_STATE_RUNNING) {
      old_thread->th_state = THREAD_STATE_READY;
      runq_enqueue_locked(my_rq, old_thread);
    }
  }

  new_thread = runq_dequeue_locked(my_rq);
  spinlock_unlock_irqrestore(&my_rq->lock, f);

  if (!new_thread) {
    new_thread = scheduler_steal_work(my_cpu);
  }

  if (!new_thread) {
    new_thread = idle_th;
  }

  if (new_thread == old_thread) {
    if (old_thread && old_thread != idle_th) {
      old_thread->th_state = THREAD_STATE_RUNNING;
    }
    return;
  }

  if (new_thread != idle_th) {
    while (__atomic_load_n(&new_thread->th_running_cpu, __ATOMIC_ACQUIRE) != 0xFFFFFFFF) {
      __asm__ volatile("pause");
    }
    new_thread->th_state = THREAD_STATE_RUNNING;
    new_thread->th_running_cpu = my_cpu;
    new_thread->th_assigned_cpu = my_cpu;
  }

  g_cpu_data[my_cpu].cpu_current_thread = new_thread;
  __asm__ volatile("mov %0, %%gs:0" :: "r"(new_thread));

  if (new_thread && new_thread->th_kernel_stack) {
    tss_set_rsp0_cpu(my_cpu, (u64)new_thread->th_kernel_stack);
  }

  extern u64 pmap_kernel_pml4(void);
  u64 new_cr3 = (new_thread && new_thread->th_task && new_thread->th_task->ta_vm_map)
                    ? (u64)new_thread->th_task->ta_vm_map
                    : pmap_kernel_pml4();

  atomic_store_explicit(&g_cpu_data[my_cpu].cpu_active_cr3,
                        new_cr3 & 0x000FFFFFFFFFF000ULL,
                        memory_order_release);

  void *dummy_sp = nullptr;
  void **saved_sp_ptr =
      (old_thread && old_thread->th_state != THREAD_STATE_HALTED)
          ? &old_thread->th_saved_sp
          : &dummy_sp;

  void *old_fp = (old_thread && old_thread->th_task && !(old_thread->th_task->ta_flags & 0x01))
                     ? old_thread->th_fp_state
                     : nullptr;
  void *new_fp = (new_thread && new_thread->th_task && !(new_thread->th_task->ta_flags & 0x01))
                     ? new_thread->th_fp_state
                     : nullptr;

  u32 *old_cpu_ptr = (old_thread && old_thread != idle_th)
                         ? (u32 *)&old_thread->th_running_cpu
                         : nullptr;

  context_switch(saved_sp_ptr, new_thread->th_saved_sp, new_cr3,
                 old_fp, new_fp, old_cpu_ptr);
}

void thread_wake(chimera_thread_t *thread) {
  if (!thread) return;

  thread_state_t expected = THREAD_STATE_WAITING;
  if (!__atomic_compare_exchange_n(&thread->th_state, &expected, THREAD_STATE_READY,
                                   false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
    return;
  }

  u32 boosted = thread->th_base_priority + 16;
  if (boosted > 95) boosted = 95;
  thread->th_sched_priority = boosted;
  thread->th_priority = boosted;

  extern u32 smp_get_cpu_count(void);
  u32 total = smp_get_cpu_count();
  u32 target_cpu = thread->th_assigned_cpu;
  if (target_cpu >= total || (target_cpu != 0 && !g_cpu_data[target_cpu].cpu_is_active)) {
    target_cpu = smp_current_cpu_id();
    thread->th_assigned_cpu = target_cpu;
  }

  cpu_runq_t *rq = &s_cpu_runq[target_cpu];
  irq_flags_t f = spinlock_lock_irqsave(&rq->lock);
  runq_enqueue_locked(rq, thread);
  spinlock_unlock_irqrestore(&rq->lock, f);

  extern void smp_send_reschedule(u32 cpu_id);
  chimera_thread_t *target_curr = g_cpu_data[target_cpu].cpu_current_thread;
  if (!target_curr || target_curr == g_cpu_data[target_cpu].cpu_idle_thread) {
    smp_send_reschedule(target_cpu);
  }
}


// ── timer-based sleep ────────────────────────────────────────────────────────
// Threads sleep on a global deadline list instead of burning scheduler
// quanta in poll loops. The PIT interrupt calls timer_wake_sleepers() to
// move expired threads back to READY; direct thread_wake() calls (signals,
// wait queues) also release sleepers early — callers must re-check their
// condition after every wake.

static chimera_thread_t *s_sleep_list = nullptr; // singly-linked via th_wait_next
static spinlock_t s_sleep_lock = SPINLOCK_INIT;

static void sleep_list_remove(chimera_thread_t *th) {
  chimera_thread_t **pp = &s_sleep_list;
  while (*pp) {
    if (*pp == th) {
      *pp = th->th_wait_next;
      th->th_wait_next = nullptr;
      return;
    }
    pp = &(*pp)->th_wait_next;
  }
}

// Park the calling thread until uptime_ms reaches deadline_ms (or an early
// wake from thread_wake). Returns after at least one deschedule.
void thread_sleep_until(u64 deadline_ms) {
  chimera_thread_t *th = current_thread();
  if (!th) return;

  irq_flags_t f = spinlock_lock_irqsave(&s_sleep_lock);
  sleep_list_remove(th); // never queue twice
  th->th_sleep_deadline = deadline_ms;
  th->th_wait_next = s_sleep_list;
  s_sleep_list = th;
  th->th_state = THREAD_STATE_WAITING;
  spinlock_unlock_irqrestore(&s_sleep_lock, f);

  scheduler_yield();
}

// Register an already-parked thread (e.g. sleeping on a wait queue) on the
// timer sleep list so bounded waits expire even without an explicit wake.
// Does not change the thread state and does not deschedule.
void thread_sleep_list_add(chimera_thread_t *th, u64 deadline_ms) {
  if (!th) return;
  irq_flags_t f = spinlock_lock_irqsave(&s_sleep_lock);
  sleep_list_remove(th);
  th->th_sleep_deadline = deadline_ms;
  th->th_wait_next = s_sleep_list;
  s_sleep_list = th;
  spinlock_unlock_irqrestore(&s_sleep_lock, f);
}

// called from the timer interrupt with interrupts disabled
void timer_wake_sleepers(void) {
  extern u64 timer_get_uptime_ms(void);
  u64 now = timer_get_uptime_ms();

  chimera_thread_t *expired = nullptr;
  irq_flags_t f = spinlock_lock_irqsave(&s_sleep_lock);
  chimera_thread_t **pp = &s_sleep_list;
  while (*pp) {
    chimera_thread_t *th = *pp;
    if ((i64)(th->th_sleep_deadline - now) <= 0) {
      *pp = th->th_wait_next;
      th->th_wait_next = expired; // reuse as temp chain
      expired = th;
    } else {
      pp = &th->th_wait_next;
    }
  }
  spinlock_unlock_irqrestore(&s_sleep_lock, f);

  while (expired) {
    chimera_thread_t *next = expired->th_wait_next;
    expired->th_wait_next = nullptr;
    thread_wake(expired);
    expired = next;
  }
}

CHIMERA_NORETURN void scheduler_ap_run(void) {
  u32 my_cpu = smp_current_cpu_id();
  chimera_thread_t *idle = g_cpu_data[my_cpu].cpu_idle_thread;
  idle->th_state = THREAD_STATE_RUNNING;
  g_cpu_data[my_cpu].cpu_current_thread = idle;
  __asm__ volatile("mov %0, %%gs:0" :: "r"(idle));
  tss_set_rsp0_cpu(my_cpu, (u64)idle->th_kernel_stack);

  extern u64 pmap_kernel_pml4(void);
  atomic_store_explicit(&g_cpu_data[my_cpu].cpu_active_cr3,
                        pmap_kernel_pml4() & 0x000FFFFFFFFFF000ULL,
                        memory_order_release);

  scheduler_yield();
  idle_thread_entry();
  CHIMERA_UNREACHABLE();
}

CHIMERA_NORETURN void scheduler_run(void) {
  chimera_thread_t *th = current_thread();
  if (!th) {
    kprintf("[CHIMERA] No initial thread. Entering idle loop.\n");
    idle_thread_entry();
  }

  kprintf("[CHIMERA] Starting scheduler. Initial task: %s\n",
          th->th_task->ta_proc->p_comm);

  void *dummy_sp;
  u64 current_cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));

  u64 new_cr3 =
      th->th_task->ta_vm_map ? (u64)th->th_task->ta_vm_map : current_cr3;

  th->th_state = THREAD_STATE_RUNNING;
  th->th_running_cpu = 0;
  th->th_assigned_cpu = 0;
  __asm__ volatile("mov %0, %%gs:0" :: "r"(th));

  tss_set_rsp0_cpu(0, (u64)th->th_kernel_stack);

  atomic_store_explicit(&g_cpu_data[0].cpu_active_cr3,
                        new_cr3 & 0x000FFFFFFFFFF000ULL,
                        memory_order_release);

  context_switch(&dummy_sp, th->th_saved_sp, new_cr3, nullptr, th->th_fp_state, nullptr);
  CHIMERA_UNREACHABLE();
}
