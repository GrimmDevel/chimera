# Chimera OS Documentation

System documentation index for Chimera OS.

- [Architecture](architecture.md) — Kernel address space, Ring 0/Ring 3 isolation, memory layout
- [Mach IPC](mach_ipc.md) — Mach ports, port rights, message passing, out-of-line (OOL) memory
- [SMP and Scheduler](smp_scheduler.md) — Multi-core boot, Local APIC, IPI vectors, priority decay scheduler
- [VFS](vfs.md) — Vnodes, DevFS (`/dev`), FAT32 persistent storage, pipes, pseudoterminals
- [Networking](networking.md) — mbufs, ifnet, Intel e1000 driver, ARP, IPv4, ICMP, UDP, TCP, DHCP, BSD sockets
- [ChimeraKit](chimerakit.md) — PCI enumeration, xHCI USB 3.0 controller, AppleHIDDriver, framebuffer console
- [Userspace](userspace.md) — libsystem, Mach-O/ELF loader, process lifecycle, Darwin hierarchy, userland tools
