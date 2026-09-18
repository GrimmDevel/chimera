// thread scheduler
#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/spinlock.h>
#include <arch/x86_64/msr.h>

extern void kprintf(const char *fmt, ...);
extern void context_switch(void **old_sp, void *new_sp, u64 new_cr3, void *old_fp, void *new_fp, void *old_running_cpu);
extern u32 smp_current_cpu_id(void);
extern void *kalloc(usize size);
extern void kfree(void *ptr);

#define SCHED_RUNQ_INIT_CAP 64

static chimera_thread_t **g_run_queue = nullptr; // heap-grown array of pointers
static u32 g_run_count = 0;
static u32 g_runq_cap = 0;
static u32 g_run_index = 0;
static spinlock_t s_runq_lock = SPINLOCK_INIT;

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

void scheduler_init(void) {
  cpu_init_syscall();
  g_run_count = 0;
  g_run_index = 0;
  
  wrmsr(MSR_GS_BASE, (u64)&cpu_local_bsp);
  wrmsr(MSR_KERNEL_GS_BASE, 0);
}

void scheduler_set_initial(chimera_thread_t *th) {
  th->th_running_cpu = 0;
  th->th_state = THREAD_STATE_RUNNING;
  cpu_local_bsp.cpu_current_thread = th;
}

int scheduler_add_thread(chimera_thread_t *th) {
  if (!th) return -1;
  irq_flags_t f = spinlock_lock_irqsave(&s_runq_lock);

  // grow the run queue on demand; only a failed allocation refuses a thread
  if (g_run_count >= g_runq_cap) {
    u32 new_cap = g_runq_cap ? g_runq_cap * 2 : SCHED_RUNQ_INIT_CAP;
    chimera_thread_t **nq =
        (chimera_thread_t **)kalloc(new_cap * sizeof(chimera_thread_t *));
    if (!nq) {
      spinlock_unlock_irqrestore(&s_runq_lock, f);
      kprintf("scheduler_add_thread: out of memory growing run queue\n");
      return -1;
    }
    if (g_run_queue) {
      __builtin_memcpy(nq, g_run_queue, g_run_count * sizeof(chimera_thread_t *));
      kfree(g_run_queue);
    }
    g_run_queue = nq;
    g_runq_cap = new_cap;
  }

  if (th->th_base_priority == 0) th->th_base_priority = 32;
  th->th_sched_priority = th->th_base_priority;
  th->th_priority = th->th_sched_priority;
  th->th_cpu_usage = 0;
  th->th_running_cpu = 0xFFFFFFFF;
  th->th_state = THREAD_STATE_READY;

  g_run_queue[g_run_count++] = th;
  spinlock_unlock_irqrestore(&s_runq_lock, f);
  return 0;
}

void scheduler_remove_thread(chimera_thread_t *th) {
  if (!th) return;
  irq_flags_t f = spinlock_lock_irqsave(&s_runq_lock);
  for (u32 i = 0; i < g_run_count; i++) {
    if (g_run_queue[i] == th) {
      for (u32 j = i; j < g_run_count - 1; j++) {
        g_run_queue[j] = g_run_queue[j + 1];
      }
      g_run_count--;
      if (g_run_index >= g_run_count && g_run_count > 0) {
        g_run_index = 0;
      }
      break;
    }
  }
  spinlock_unlock_irqrestore(&s_runq_lock, f);
}

chimera_thread_t *current_thread(void) {
  chimera_thread_t *th = nullptr;
  __asm__ volatile("mov %%gs:0, %0" : "=r"(th));
  return th;
}

void scheduler_yield(void) {
  u32 my_cpu = smp_current_cpu_id();
  irq_flags_t f = spinlock_lock_irqsave(&s_runq_lock);

  chimera_thread_t *old_thread = current_thread();
  chimera_thread_t *new_thread = nullptr;

  if (old_thread) {
    old_thread->th_cpu_usage++;
    if (old_thread->th_sched_priority > old_thread->th_base_priority / 2) {
      old_thread->th_sched_priority--;
    }
    old_thread->th_priority = old_thread->th_sched_priority;
    if (old_thread->th_state == THREAD_STATE_RUNNING) {
      old_thread->th_state = THREAD_STATE_READY;
      old_thread->th_running_cpu = 0xFFFFFFFF;
    }
  }

  u32 best_pri = 0;
  u32 best_index = 0;

  if (g_run_count > 0) {
    for (u32 offset = 1; offset <= g_run_count; offset++) {
      u32 i = (g_run_index + offset) % g_run_count;
      chimera_thread_t *th = g_run_queue[i];
      if (th && th->th_state == THREAD_STATE_READY &&
          (th->th_running_cpu == 0xFFFFFFFF || th->th_running_cpu == my_cpu)) {
        if (!new_thread || th->th_priority > best_pri) {
          new_thread = th;
          best_pri = th->th_priority;
          best_index = i;
        }
      }
    }
  }

  if (new_thread) {
    g_run_index = best_index;
    new_thread->th_state = THREAD_STATE_RUNNING;
    new_thread->th_running_cpu = my_cpu;
  } else if (old_thread && old_thread->th_state != THREAD_STATE_HALTED) {
    old_thread->th_state = THREAD_STATE_RUNNING;
    old_thread->th_running_cpu = my_cpu;
    new_thread = old_thread;
  }

  if (!new_thread) {
    if (old_thread && old_thread->th_state == THREAD_STATE_HALTED) {
      old_thread->th_running_cpu = 0xFFFFFFFF;
    }
    if (my_cpu < 16 && g_cpu_data[my_cpu].cpu_kernel_stack) {
      tss_set_rsp0_cpu(my_cpu, (u64)g_cpu_data[my_cpu].cpu_kernel_stack);
      g_cpu_data[my_cpu].cpu_current_thread = nullptr;
          void *null_th = nullptr;
      __asm__ volatile("mov %0, %%gs:0" :: "r"(null_th));
      u64 idle_sp = (u64)g_cpu_data[my_cpu].cpu_kernel_stack;
      spinlock_unlock_irqrestore(&s_runq_lock, f);
      __asm__ volatile(
          "mov %0, %%rsp\n"
          "1:\n"
          "sti\n"
          "hlt\n"
          "call _scheduler_yield\n"
          "jmp 1b\n"
          : : "r"(idle_sp) : "memory");
    }
    spinlock_unlock_irqrestore(&s_runq_lock, f);
    return;
  }

  if (old_thread && old_thread->th_state == THREAD_STATE_HALTED) {
    old_thread->th_running_cpu = 0xFFFFFFFF;
  }

  if (new_thread == old_thread) {
    old_thread->th_state = THREAD_STATE_RUNNING;
    old_thread->th_running_cpu = my_cpu;
    spinlock_unlock_irqrestore(&s_runq_lock, f);
    return;
  }

  if (my_cpu < 16) {
    g_cpu_data[my_cpu].cpu_current_thread = new_thread;
  }
  // current_thread() // per-CPU removed: per-CPU gs:[0] (cpu_current_thread) is the
  // only source of truth. The global mirror was a race on SMP.

  __asm__ volatile("mov %0, %%gs:0" :: "r"(new_thread));

  tss_set_rsp0_cpu(my_cpu, (u64)new_thread->th_kernel_stack);

  spinlock_unlock(&s_runq_lock);

  extern u64 pmap_kernel_pml4(void);
  u64 new_cr3 = (new_thread->th_task && new_thread->th_task->ta_vm_map)
                    ? (u64)new_thread->th_task->ta_vm_map
                    : pmap_kernel_pml4();

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

  u32 *old_cpu_ptr =
      (old_thread && old_thread->th_state == THREAD_STATE_READY)
          ? &old_thread->th_running_cpu
          : nullptr;

  context_switch(saved_sp_ptr, new_thread->th_saved_sp, new_cr3,
                 old_fp, new_fp, old_cpu_ptr);
}

void thread_wake(chimera_thread_t *thread) {
  if (!thread) return;
  irq_flags_t f = spinlock_lock_irqsave(&s_runq_lock);
  if (thread->th_running_cpu != 0xFFFFFFFF || thread->th_state == THREAD_STATE_RUNNING) {
    spinlock_unlock_irqrestore(&s_runq_lock, f);
    return;
  }
  u32 boosted = thread->th_base_priority + 16;
  if (boosted > 95) boosted = 95;
  thread->th_sched_priority = boosted;
  thread->th_priority = boosted;
  thread->th_state = THREAD_STATE_READY;
  thread->th_running_cpu = 0xFFFFFFFF;
  spinlock_unlock_irqrestore(&s_runq_lock, f);
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
  for (;;) {
    scheduler_yield();
    __asm__ volatile("sti; hlt");
  }
  CHIMERA_UNREACHABLE();
}

CHIMERA_NORETURN void scheduler_run(void) {
  chimera_thread_t *th = current_thread();
  if (!th) {
    kprintf("[CHIMERA] No initial thread. Entering idle loop.\n");
    __asm__ volatile("sti");
    for (;;)
      __asm__ volatile("hlt");
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
  __asm__ volatile("mov %0, %%gs:0" :: "r"(th));

  tss_set_rsp0_cpu(0, (u64)th->th_kernel_stack);

  context_switch(&dummy_sp, th->th_saved_sp, new_cr3, nullptr, th->th_fp_state, nullptr);
  CHIMERA_UNREACHABLE();
}
