# SMP and Thread Scheduler

Multi-core x86_64 Symmetric Multiprocessing (SMP) architecture and priority-decay thread scheduler.

## Multi-Core Bootstrap
- Limine SMP protocol enumerates up to 16 cores (`CHIMERA_MAX_CPUS`).
- **Bootstrap Processor (BSP)**: Sets up Local APIC, GDT, TSS, IDT, system call MSRs (`MSR_LSTAR`, `MSR_STAR`, `MSR_FMASK`), and maps `cpu_local_t` struct to `MSR_GS_BASE`.
- **Application Processors (AP)**: Bootstrapped via Limine AP trampolines; each AP loads its own per-CPU GDT/TSS, IDT, configures syscall MSRs, and enters `scheduler_ap_run()`.

## Per-CPU Data (`cpu_local_t`)
Each core owns a private `cpu_local_t` structure in `g_cpu_data[cpu_id]`:
- Offset `0x00`: `cpu_current_thread` (fast thread lookup via `%gs:0`)
- Offset `0x08`: `cpu_user_rsp_save` (temporary storage for user `%rsp` upon `syscall` entry)
- Offset `0x10`: `cpu_id` (current core logical ID)
- Offset `0x14`: `cpu_lapic_id`
- Offset `0x20`: `cpu_kernel_stack` (dedicated idle/kernel stack per core)
- Offset `0x38`: `cpu_tss_ptr`

## Inter-Processor Interrupts (IPI)
- `VECTOR_IPI_SCHED` (`0xEE`): Signals target core to perform a preemptive reschedule.
- `VECTOR_IPI_TLB` (`0xEF`): Coordinates TLB shootdowns by forcing a CR3 reload across all active cores.
- `VECTOR_SPURIOUS` (`0xFF`): Handles spurious APIC interrupts.

## Thread Scheduler State Machine
- `g_run_queue`: Global run queue protected by `s_runq_lock` (`irqsave` spinlock).
- `th_running_cpu`: Tracks which core is actively running on the thread stack. Atomically updated inside `context_switch` (`switch.S`) after stack pointer swap to prevent concurrent multi-core execution of the same thread.
- `thread_wake()`: Safely transitions waiting threads to `THREAD_STATE_READY` with an interactive priority boost; ignores threads that are already executing on a core.

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

## Priority Management
- `th_base_priority`: Static base priority assigned at thread creation (default 32 for user processes).
- `th_sched_priority`: Dynamic priority that decays based on CPU consumption (`th_cpu_usage`).
- `thread_wake()`: Applies an interactive boost (`+16` priority, clamped at 95) to unblock GUI and interactive console threads promptly.
