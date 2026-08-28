# Chimera OS Architecture

Hybrid Mach/BSD kernel for x86_64, modeled after Apple XNU / Darwin.

## Memory Map
- Limine protocol loads kernel to higher-half `-2GB` (`0xFFFFFFFF80000000`)
- `g_hhdm_base` maps all physical memory 1:1 at `0xFFFF800000000000` (HHDM)
- Userspace virtual address space covers `0x0000000000001000` to `0x00007FFFFFFFFFFF`
- User stack grows down from `0x00007FFFFFFF0000`
- User heap / mmap allocator starts at `0x0000100000000000`

```
+------------------------------------+ 0xFFFFFFFFFFFFFFFF
| Kernel binary and text             | (high 2GB: 0xFFFFFFFF80000000)
+------------------------------------+
| Higher-Half Direct Map (HHDM)      | (0xFFFF800000000000 - all phys RAM)
+------------------------------------+
| Canonical hole (non-canonical)     |
+------------------------------------+
| Userspace stack (grows down)       | 0x00007FFFFFFF0000
| Userspace mmap / heap / bss        | 0x0000100000000000
| Userspace text / data / Mach-O     | 0x0000000000001000
+------------------------------------+ 0x0000000000000000
```

## Kernel Subsystems
1. **Mach Core**:
   - `chimera_task_t`: Resource container, `vm_map`, port space (`ipc_space_t`), credentials
   - `chimera_thread_t`: Execution context, kernel stack, saved registers, FPU/SSE state, scheduling priority
   - `ipc_port_t`: Communication endpoint with locked message queue
   - `vm_map` / `vm_object`: Address space management with copy-on-write (COW) page tables and zero-copy OOL transfer
2. **BSD Personality**:
   - `chimera_proc_t`: POSIX process structure, PID, parent/child relationships, signal actions, credentials (`uid`, `gid`)
   - `fileproc_t` / `fd_table`: Per-process file descriptor table for vnodes, sockets, pipes, and PTYs
   - `syscall`: System call dispatcher via x86_64 `syscall` / `sysretq` instructions (`MSR_LSTAR`)
3. **Virtual File System (VFS)**:
   - `vnode_t` abstraction for rootfs, DevFS, FAT32 on ATA, pipes, and pseudoterminals
4. **ChimeraKit Driver Framework**:
   - C++ driver layer for PCI bus enumeration, xHCI USB 3.0 host controller, AppleHIDDriver, display controller
5. **Chimera-Net**:
   - Darwin-style socket layer (`socket_t`), `mbuf` packet chains and clusters, `ifnet` network interfaces, Intel e1000 driver

## Ring 0 / Ring 3 Isolation & Security
- Userspace syscall entry via `MSR_LSTAR` pointing to `x86_64_syscall_entry` in `syscall_entry.S`
- `swapgs` switches between user `%gs` and per-CPU `cpu_local_t` struct
- Kernel stack loaded into TSS (`tss_set_rsp0_cpu`) on task switch
- `copyin()` and `copyout()` enforce boundary validation (`0x1000` to `0x7FFFFFFFFFFF`); passing kernel virtual addresses returns `-EFAULT`
- User faults (divide-by-zero, segfault, invalid opcode) deliver signals (`SIGSEGV`, `SIGFPE`, `SIGILL`) to the process without crashing the kernel
- Full resource reclamation on `exit()`: physical pages, intermediate page tables, and file descriptors are freed to prevent memory leaks
