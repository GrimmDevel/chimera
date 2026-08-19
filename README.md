# xiu

x86_64 mach/bsd hybrid kernel inspired by xnu/darwin. boots with limine protocol.

## what is this
- mach ipc (ports, port rights, msg queues, ool memory)
- bsd layer (posix syscalls, vfs, process tree, signals, pipes, pty)
- vfs (devfs, fat32 on ata, rootfs hierarchy)
- smp scheduler (multi-core apic/ipi, dynamic decay, per-cpu threads)
- net (bsd socket layer, mbufs, arp, icmp, udp, tcp, e1000 driver, dhcp)
- xiukit driver model (pci discovery, xhci usb 3.0, ps2 hid fallback, fb console)
- userspace with libsystem, mach-o / elf loader, dash/xsh shell, tcc, kilo, gui window server

## requirements
- clang / llvm / lld
- cmake >= 3.20, gmake / make
- qemu-system-x86_64
- xorriso, mtools (for iso and fat32 disk image)
- limine (submodule / binary in repo)

## build and run
```sh
# build kernel and userspace binaries
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# make disk image and iso
make disk iso

# run in qemu
make run
```
or just make run to rebuild all and run

## directory layout
- `kernel/`
  - `arch/x86_64/` - idt, gdt, pmap, smp, lapic, scheduler, boot entry
  - `mach/` - mach ports, ipc message queues, vm map/object, mach traps
  - `bsd/` - bsd syscall dispatch, proc table, signal delivery, mach/elf loader
  - `vfs/` - vfs vnodes, devfs (/dev/console, /dev/null, /dev/mouse), fat32, pipes, pty
  - `net/` - darwin-style mbuf allocator, ifnet, e1000 nic driver, arp, ip, icmp, udp, tcp, dhcp
  - `xiukit/` - pci bus scanner, xhci usb host controller, hid mouse/kbd driver
  - `console.c` - framebuffer text renderer with scrollback + serial com1 mirror
- `usr/`
  - `libsystem/` - libc / libmach implementation (syscall stubs, malloc, stdio, string, mach ipc api)
  - `bin/` - userland tools (sh, dash, ls, cat, mkdir, rm, touch, kilo, tcc, curl, nc, ping, ifconfig, wserver, guiapp, calc, smpdemo, machdemo)
- `scripts/` - qemu launch script (`run_qemu.sh`), iso generation scripts
- `build/` - cmake build output, disk.img, xiu-x86_64.iso

## debug
```sh
# run qemu with gdb stub on :1234
./scripts/run_qemu.sh x86_64 1

# connect gdb
gdb build/x86_64/kernel/xiu_kernel.elf -ex "target remote :1234"
```

## documentation
see [`docs/`](docs/README.md) for subsystem documentation:
- [`docs/architecture.md`](docs/architecture.md)
- [`docs/mach_ipc.md`](docs/mach_ipc.md)
- [`docs/smp_scheduler.md`](docs/smp_scheduler.md)
- [`docs/vfs.md`](docs/vfs.md)
- [`docs/networking.md`](docs/networking.md)
- [`docs/xiukit.md`](docs/xiukit.md)
- [`docs/userspace.md`](docs/userspace.md)

  yep parts of it is vibecoded cuz im too bored and too lazy
