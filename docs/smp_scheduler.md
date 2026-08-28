# smp and scheduler

multi-core x86_64 symmetric multiprocessing and priority-decay scheduler.

## multi-core bootstrap
- limine smp protocol discovers up to 16 cores (`CHIMERA_MAX_CPUS`).
- bsp initializes local apic, gdt, idt, syscall msrs, and per-cpu `cpu_local_t` struct mapped at `MSR_GS_BASE`.
- ap cores are started via limine trampolines, initialize their own per-cpu gdt/tss, load idt, configure syscall msrs, and enter `scheduler_ap_run()`.

## per-cpu data (`cpu_local_t`)
each core has private `cpu_local_t` in `g_cpu_data[cpu_id]`:
- offset `0x00`: `cpu_current_thread` (fast access via `%gs:0`)
- offset `0x08`: `cpu_user_rsp_save`
- offset `0x10`: `cpu_id` (read via `%gs:0x10`)
- offset `0x14`: `cpu_lapic_id`
- offset `0x38`: `cpu_tss_ptr`

## ipi vectors
- `VECTOR_IPI_SCHED` (`0xEE`): triggers preemptive context switch / reschedule on target core.
- `VECTOR_IPI_TLB` (`0xEF`): forces cr3 reload to invalidate tlb caches across all cores.
- `VECTOR_SPURIOUS` (`0xFF`): spurious interrupt handler.

## thread scheduler
- `g_run_queue`: array of ready/running threads protected by `s_runq_lock` (irqsave spinlock).
- `th_running_cpu`: tracks which core currently owns the thread stack. Prevents two cores from running the same thread concurrently.
- idle cores execute `sti; hlt` waiting for interrupts or new tasks.

```
Thread State Diagram:
 [Created] -> THREAD_STATE_READY
                    |
               (scheduler_yield)
                    v
          THREAD_STATE_RUNNING (owned by th_running_cpu)
             /             \
      (yield/preempt)   (sleep/wait)
           v                 v
  THREAD_STATE_READY    THREAD_STATE_WAITING
                             |
                       (thread_wake)
                             v
                    THREAD_STATE_READY
```

## priority management
- `th_base_priority`: initial static base priority (default 32 for userland).
- `th_sched_priority`: effective priority with dynamic decay on heavy cpu usage (`th_cpu_usage`).
- `thread_wake()`: gives interactive boost (`+16` priority, clamped at 95) to unblock ui/interactive threads immediately.
