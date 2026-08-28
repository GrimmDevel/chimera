# xiu architecture

hybrid mach/bsd kernel for x86_64. heavily modeled after apple xnu / darwin.

## memory map
- limine protocol loads kernel to higher-half `-2GB` (`0xFFFFFFFF80000000`)
- `g_hhdm_base` maps all physical memory 1:1 at `0xFFFF800000000000`
- userspace address space is `0x0000000000000000` to `0x00007FFFFFFFFFFF`
- user stack grows down from `0x00007FFFFFFFE000`
- user heap / mmap starts at `0x0000100000000000`

```
+------------------------------------+ 0xFFFFFFFFFFFFFFFF
| Kernel binary and text             | (high 2GB: 0xFFFFFFFF80000000)
+------------------------------------+
| Higher-Half Direct Map (HHDM)      | (0xFFFF800000000000 - all phys RAM)
+------------------------------------+
| Canonical hole (non-canonical)     |
+------------------------------------+
| Userspace stack (grows down)       | 0x00007FFFFFFFF000
| Userspace mmap / heap / bss        | 0x0000100000000000
| Userspace text / data / elf        | 0x0000000000400000
+------------------------------------+ 0x0000000000000000
```

## kernel layers
1. **mach core**:
   - `task`: resource container, vm_map, port space, credentials
   - `thread`: execution context, kernel stack, saved registers, sched state
   - `ipc_port`: communication endpoint with message queue
   - `vm_map` / `vm_object`: address space tracking with copy-on-write page tables
2. **bsd personality**:
   - `proc`: posix process structure, pid, parent/child relationships, signal handlers
   - `fileproc` / `fd_table`: per-process file descriptor table
   - `syscall`: syscall dispatcher via x86_64 `syscall` / `sysretq` instructions
3. **vfs**:
   - `vnode` abstraction for rootfs, devfs, fat32, pipes, and pseudoterminals
4. **chimerakit**:
   - c++ driver layer for pci discovery, xhci usb host controllers, hid input, framebuffer
5. **net**:
   - darwin-style socket layer (`socket_t`), `mbuf` packet chains, `ifnet` network interfaces

## ring 0 / ring 3 isolation
- userspace syscall entry via `MSR_LSTAR` pointing to `syscall_entry` in `switch.S`
- `swapgs` switches to per-cpu `cpu_local_t` struct
- kernel stack loaded from `tss_set_rsp0()` on task switch
- `copyin()` and `copyout()` validate all user pointers against page table mappings; invalid pointers return `-EFAULT`
- user faults (div-by-zero, segfault, illegal instruction) dispatch signals (`SIGSEGV`, `SIGFPE`, `SIGILL`) to the faulting process without crashing the kernel
