// thread scheduler
#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/spinlock.h>
#include <arch/x86_64/msr.h>

extern void kprintf(const char *fmt, ...);
extern void context_switch(void **old_sp, void *new_sp, u64 new_cr3);
extern u32 smp_current_cpu_id(void);

#define SCHED_MAX_THREADS 64

static xiu_thread_t *g_current_thread = nullptr;
static xiu_thread_t *g_run_queue[SCHED_MAX_THREADS];
static u32 g_run_count = 0;
static u32 g_run_index = 0;
static spinlock_t s_runq_lock = SPINLOCK_INIT;

extern void task_switch_to_user(uptr entry, uptr stack);
extern void task_switch_to_user_frame(uptr entry, uptr stack, void *frame, u64 rax);
extern void tss_set_rsp0(u64 rsp0);

static void thread_launcher(void);
static void fork_thread_launcher(void);

void thread_init_stack(xiu_thread_t *th, void *entry, void *stack) {
  extern xiu_paddr_t pmm_alloc_pages(usize count);
  extern u64 g_hhdm_base;

  // allocate 4 pages per thread
  xiu_paddr_t paddr = pmm_alloc_pages(4);
  if (paddr == (xiu_paddr_t)-1 || paddr == 0) {
    xiu_panic(
        "thread_init_stack: PMM out of pages — cannot allocate kernel stack\n");
  }

  u8 *kstack_base = (u8 *)(g_hhdm_base + paddr);
  void *kstack_top = kstack_base + (4 * XIU_PAGE_SIZE);

  th->th_stack_base = kstack_base;
  th->th_stack_size = 4 * XIU_PAGE_SIZE;
  th->th_kernel_stack = kstack_top;
  th->th_running_cpu = 0xFFFFFFFF;

  u64 *sp = (u64 *)kstack_top;
  *(--sp) = (u64)thread_launcher;
  *(--sp) = 0; // rbp
  *(--sp) = 0; // rbx
  *(--sp) = 0; // r12
  *(--sp) = 0; // r13
  *(--sp) = 0; // r14
  *(--sp) = 0; // r15

  th->th_saved_sp = sp;
  th->th_context    = entry;
  th->th_user_stack = stack;
}

void thread_init_fork_stack(xiu_thread_t *th, void *entry, void *stack) {
  thread_init_stack(th, entry, stack);
  
  u64 *sp = (u64 *)th->th_saved_sp;
  sp[6] = (u64)fork_thread_launcher;
}

static void thread_launcher(void) {
  xiu_thread_t *th = current_thread();
  
  tss_set_rsp0((u64)th->th_kernel_stack);

  u64 entry = (u64)th->th_context;
  u64 stack = (u64)th->th_user_stack;
  
  task_switch_to_user(entry, stack);
  XIU_UNREACHABLE();
}

static void fork_thread_launcher(void) {
  xiu_thread_t *th = current_thread();
  tss_set_rsp0((u64)th->th_kernel_stack);
  
  u64 fork_return_value = th->th_is_fork_child ? th->th_fork_return_value : 0;
  
  task_switch_to_user_frame((uptr)th->th_context, (uptr)th->th_user_stack,
                            th->th_user_frame, fork_return_value);
  XIU_UNREACHABLE();
}

extern void cpu_init_syscall(void);

void scheduler_init(void) {
  cpu_init_syscall();
  g_current_thread = nullptr;
  g_run_count = 0;
  g_run_index = 0;
  
  wrmsr(MSR_GS_BASE, (u64)&cpu_local_bsp);
  wrmsr(MSR_KERNEL_GS_BASE, 0);
}

void scheduler_set_initial(xiu_thread_t *th) {
  th->th_running_cpu = 0;
  th->th_state = THREAD_STATE_RUNNING;
  g_current_thread = th;
  cpu_local_bsp.cpu_current_thread = th;
}

void scheduler_add_thread(xiu_thread_t *th) {
  irq_flags_t f = spinlock_lock_irqsave(&s_runq_lock);
  if (g_run_count >= SCHED_MAX_THREADS) {
    spinlock_unlock_irqrestore(&s_runq_lock, f);
    xiu_panic("scheduler_add_thread: run queue full (max %u threads)\n",
              SCHED_MAX_THREADS);
  }
  if (th->th_base_priority == 0) th->th_base_priority = 32;
  th->th_sched_priority = th->th_base_priority;
  th->th_priority = th->th_sched_priority;
  th->th_cpu_usage = 0;
  th->th_running_cpu = 0xFFFFFFFF;
  th->th_state = THREAD_STATE_READY;

  g_run_queue[g_run_count++] = th;
  spinlock_unlock_irqrestore(&s_runq_lock, f);
}

void scheduler_remove_thread(xiu_thread_t *th) {
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

xiu_thread_t *current_thread(void) {
  xiu_thread_t *th = nullptr;
  __asm__ volatile("mov %%gs:0, %0" : "=r"(th));
  return th ? th : g_current_thread;
}

void scheduler_yield(void) {
  u32 my_cpu = smp_current_cpu_id();
  irq_flags_t f = spinlock_lock_irqsave(&s_runq_lock);

  xiu_thread_t *old_thread = current_thread();
  xiu_thread_t *new_thread = nullptr;

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

  for (u32 offset = 1; offset <= g_run_count; offset++) {
    u32 i = (g_run_index + offset) % g_run_count;
    xiu_thread_t *th = g_run_queue[i];
    if (th->th_state == THREAD_STATE_READY &&
        (th->th_running_cpu == 0xFFFFFFFF || th->th_running_cpu == my_cpu)) {
      if (!new_thread || th->th_priority > best_pri) {
        new_thread = th;
        best_pri = th->th_priority;
        best_index = i;
      }
    }
  }

  if (new_thread) {
    g_run_index = best_index;
    new_thread->th_state = THREAD_STATE_RUNNING;
    new_thread->th_running_cpu = my_cpu;
  } else if (old_thread) {
    old_thread->th_state = THREAD_STATE_RUNNING;
    old_thread->th_running_cpu = my_cpu;
    new_thread = old_thread;
  }

  if (!new_thread) {
    spinlock_unlock_irqrestore(&s_runq_lock, f);
    return;
  }

  if (new_thread == old_thread) {
    spinlock_unlock_irqrestore(&s_runq_lock, f);
    return;
  }

  if (my_cpu < 16) {
    g_cpu_data[my_cpu].cpu_current_thread = new_thread;
  }
  if (my_cpu == 0) {
    g_current_thread = new_thread;
  }

  __asm__ volatile("mov %0, %%gs:0" :: "r"(new_thread));

  spinlock_unlock_irqrestore(&s_runq_lock, f);

  tss_set_rsp0((u64)new_thread->th_kernel_stack);

  u64 current_cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));
  u64 new_cr3 = new_thread->th_task->ta_vm_map
                    ? (u64)new_thread->th_task->ta_vm_map
                    : current_cr3;

  void *dummy_sp = nullptr;
  void **saved_sp_ptr = old_thread ? &old_thread->th_saved_sp : &dummy_sp;

  context_switch(saved_sp_ptr, new_thread->th_saved_sp, new_cr3);
}

void thread_wake(xiu_thread_t *thread) {
  if (!thread) return;
  irq_flags_t f = spinlock_lock_irqsave(&s_runq_lock);
  u32 boosted = thread->th_base_priority + 16;
  if (boosted > 95) boosted = 95;
  thread->th_sched_priority = boosted;
  thread->th_priority = boosted;
  thread->th_state = THREAD_STATE_READY;
  thread->th_running_cpu = 0xFFFFFFFF;
  spinlock_unlock_irqrestore(&s_runq_lock, f);
}

XIU_NORETURN void scheduler_ap_run(void) {
  for (;;) {
    scheduler_yield();
    __asm__ volatile("sti; hlt");
  }
  XIU_UNREACHABLE();
}

XIU_NORETURN void scheduler_run(void) {
  xiu_thread_t *th = current_thread();
  if (!th) {
    kprintf("[XIU] No initial thread. Entering idle loop.\n");
    __asm__ volatile("sti");
    for (;;)
      __asm__ volatile("hlt");
  }

  kprintf("[XIU] Starting scheduler. Initial task: %s\n",
          th->th_task->ta_proc->p_comm);

  void *dummy_sp;
  u64 current_cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));

  u64 new_cr3 =
      th->th_task->ta_vm_map ? (u64)th->th_task->ta_vm_map : current_cr3;

  th->th_state = THREAD_STATE_RUNNING;
  th->th_running_cpu = 0;
  __asm__ volatile("mov %0, %%gs:0" :: "r"(th));

  context_switch(&dummy_sp, th->th_saved_sp, new_cr3);
  XIU_UNREACHABLE();
}
