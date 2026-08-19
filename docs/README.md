# xiu documentation

system documentation index.

- [architecture](architecture.md) — kernel address space, ring0/ring3 isolation, memory layout
- [mach ipc](mach_ipc.md) — ports, port rights, message passing, out-of-line memory
- [smp and scheduler](smp_scheduler.md) — multi-core boot, local apic, ipi, priority decay scheduler
- [vfs](vfs.md) — vnodes, devfs (/dev), fat32 persistent storage, pipes, pseudoterminals
- [networking](networking.md) — mbufs, ifnet, intel e1000 driver, arp, ip, icmp, udp, tcp, dhcp, sockets
- [xiukit](xiukit.md) — pci enumeration, xhci usb 3.0 controller, hid input layer, framebuffer
- [userspace](userspace.md) — libsystem, mach-o/elf loader, process lifecycle, userland tools
