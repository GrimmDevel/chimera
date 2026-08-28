# Chimera OS

x86_64 hybrid Mach/BSD operating system inspired by Apple XNU / Darwin. Boots via Limine boot protocol.

## Overview
- **Mach Core Subsystem**: tasks, threads, Mach IPC ports, port rights, message queues with out-of-line (OOL) memory support, Darwin VM object and VM map subsystems with copy-on-write page tables.
- **BSD Personality Layer**: POSIX syscall layer, process tree, session and credentials management, signal delivery, pipes, unix98 pseudoterminals (PTY), VFS vnodes.
- **Darwin VFS & Storage**: in-memory `/dev` (DevFS), FAT32 filesystem driver on ATA/IDE block device with long file names (LFN) and persistent storage.
- **SMP Scheduler**: Symmetric Multiprocessing for multi-core x86_64, Local APIC and IPI subsystem (TLB shootdown, reschedule vectors), priority decay scheduling, per-CPU data structures (`cpu_local_t` on `%gs`).
- **Darwin Networking (Chimera-Net)**: BSD socket layer, `mbuf` packet chains and clusters, network interface (`ifnet`) layer, Intel 82540EM/82545EM Gigabit Ethernet driver (`e1000`), ARP, IPv4, ICMP, UDP, TCP state machine, DHCP client.
- **ChimeraKit Driver Framework**: modular C++ driver model with PCI bus discovery, xHCI USB 3.0 host controller (XHCI 1.1 with HID keyboard/mouse decoding), AppleHIDDriver, framebuffer console text renderer with shadow RAM buffer.
- **Userspace & Frameworks**: `libsystem_chimera`, Mach-O and ELF64 loader, launchd foundation (`launchd-chimera` PID 1), shells (`zsh`, `sh`), Cocoa/NextStep runtime (`objc_runtime_chimera`, `Foundation`, `AppKit`, `CoreGraphics`, `Onyx2D`), GUI WindowServer with compositor, Dock, SystemUIServer, Filer, and tools (`sectest`, `tcc`, `kilo`, `fastfetch`, `curl`, `ping`, `ifconfig`, `nc`).
- **Security & Stability**: strict Ring 0 / Ring 3 isolation, safe `copyin`/`copyout` boundary checks, zero-leak resource reclamation on process exit, regression test suite (`sectest`).

## Requirements
- Clang / LLVM / LLD
- CMake >= 3.20, GNU Make / make
- QEMU (`qemu-system-x86_64`)
- xorriso, mtools (for ISO and FAT32 disk image generation)
- Limine bootloader (included in repository)

## Build and Run
```sh
# build kernel and userspace binaries
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# make disk image and iso
make disk iso

# run in qemu
make run
```
Or simply run `make run` to build everything and launch QEMU.

## Directory Layout
- `kernel/`
  - `arch/x86_64/` - IDT, GDT, TSS, PMAP, SMP, LAPIC, scheduler, context switch, boot entry
  - `mach/` - Mach ports, IPC message queues, IPC space, VM map, VM object, Mach traps
  - `bsd/` - BSD syscall dispatch, process tables, signal delivery, credentials, Mach-O/ELF loader
  - `vfs/` - VFS vnode layer, DevFS (`/dev/console`, `/dev/null`, `/dev/mouse`, `/dev/serial`, `/dev/fb0`), FAT32, pipes, PTY
  - `net/` - Darwin-style `mbuf` allocator, `ifnet`, Intel e1000 NIC driver, ARP, IPv4, ICMP, UDP, TCP, DHCP
  - `chimerakit/` - PCI bus scanner, xHCI USB 3.0 host controller, AppleHIDDriver, display controller
  - `console.c` - Framebuffer text renderer with scrollback and serial COM1 mirror
- `usr/`
  - `libsystem/` - libc / libmach implementation (system call stubs, malloc, stdio, string, Mach IPC API)
  - `libobjc/` - Objective-C runtime for Chimera
  - `bin/` - userland tools (`zsh`, `sh`, `ls`, `cat`, `mkdir`, `rm`, `touch`, `sectest`, `kilo`, `tcc`, `curl`, `nc`, `ping`, `ifconfig`, `smpdemo`, `machdemo`, `fastfetch`)
  - `gui/` - GUI subsystem (`WindowServer`, `SystemUIServer`, `Dock`, `Filer`, `calc`, `Terminal`)
- `scripts/` - QEMU runner (`run_qemu.sh`), ISO and disk creation tools
- `build/` - CMake build artifacts, `disk.img`, `chimera-x86_64.iso`

## Testing & Security Audit
Run the kernel security and regression test suite inside Chimera OS:
```sh
sectest
```
Validates pointer boundaries, Mach OOL descriptors, IPC port lifecycle, TCP buffer invariants, privilege models (`setuid`, `kill` on PID 0/1), and pipe invariants.

## Debugging
```sh
# run qemu with gdb stub on :1234
./scripts/run_qemu.sh x86_64 1

# connect gdb
gdb build/x86_64/kernel/chimera_kernel.elf -ex "target remote :1234"
```

## Documentation
Subsystem documentation is available in [`docs/`](docs/README.md):
- [`docs/architecture.md`](docs/architecture.md) — Kernel address space, Ring 0/Ring 3 isolation, memory layout
- [`docs/mach_ipc.md`](docs/mach_ipc.md) — Mach ports, port rights, message passing, OOL memory
- [`docs/smp_scheduler.md`](docs/smp_scheduler.md) — Multi-core SMP, LAPIC, IPI, priority decay scheduler
- [`docs/vfs.md`](docs/vfs.md) — VFS vnodes, DevFS, FAT32 storage, pipes, pseudoterminals
- [`docs/networking.md`](docs/networking.md) — mbufs, ifnet, Intel e1000 driver, TCP/IP stack, DHCP
- [`docs/chimerakit.md`](docs/chimerakit.md) — ChimeraKit driver framework, PCI, xHCI USB 3.0, HID
- [`docs/userspace.md`](docs/userspace.md) — libsystem, Mach-O loader, process lifecycle, Darwin hierarchy
