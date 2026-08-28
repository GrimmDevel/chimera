# Userspace, Libsystem and Frameworks

Darwin and POSIX userspace environment, standard C library, Cocoa/NextStep runtime, and utilities.

## Libsystem (`usr/libsystem`)
`libsystem_chimera` implements POSIX and Mach APIs:
- **System Call Stubs**: Assembly entry points invoking the x86_64 `syscall` instruction.
- **Mach IPC API**: `mach_msg_trap()`, `mach_port_allocate()`, `mach_port_deallocate()`, `mach_vm_allocate()`, `mach_vm_deallocate()`, `task_get_bootstrap_port()`.
- **Memory Allocator**: Chunk-based `malloc()`, `free()`, `realloc()`, `calloc()` backed by `mmap()` pages.
- **C Standard Library**: Standard I/O (`printf`, `snprintf`, `fopen`, `fread`, `fwrite`), string manipulation, and math functions.
- **`crt0`**: Process entry point setting up arguments, environment pointers, and calling `main(argc, argv)`.

## Cocoa & Objective-C Runtime
- `usr/libobjc`: Lightweight Objective-C runtime (`objc_runtime_chimera.c`) providing class registration, method dispatch (`objc_msgSend`), selector tables, and object allocation.
- `Foundation`: Objective-C foundation classes (`NSString`, `NSArray`, `NSDictionary`, `NSData`, `NSAutoreleasePool`).
- `AppKit` & `CoreGraphics` / `Onyx2D`: 2D vector drawing, window backing stores, event dispatch, and UI controls.

## Binary Loader (`kernel/bsd/mach_loader.c`)
- Supports both 64-bit Mach-O (`MH_MAGIC_64`) and ELF64 formats.
- Maps segments (`LC_SEGMENT_64`, `PT_LOAD`) into user `vm_map` with strict page alignment.
- Allocates an 8MB user stack (`0x00007FFFFFFF0000`).
- Formats `argc`, `argv`, and `envp` onto the user stack before entering user mode via `task_switch_to_user()` / `iretq`.

## Process Lifecycle
- `fork()`: Clones process task and virtual address space using copy-on-write (COW) page sharing.
- `execve(path, argv, envp)`: Replaces user address space with a new binary image.
- `exit(code)`: Terminates execution, closes open file descriptors, frees page table trees and user physical pages, sends `SIGCHLD` to parent.
- `waitpid(pid, status, options)`: Reaps zombie process exit statuses and frees process table slots.

## System Tools & Applications
- **Launchd Foundation**: `launchd-chimera` (PID 1) initializes system daemons, console login sessions, and shell environments.
- **Shells**: `zsh` and `sh` (POSIX Almquist shell).
- **Security & Quality Audit**: `sectest` — regression test suite covering memory validation, Mach IPC bounds, and privilege checks.
- **System Diagnostics**: `fastfetch`, `flashfetch`, `neofetch`, `proclist`, `uptime`, `stat`, `df`.
- **Networking Tools**: `ping`, `ifconfig`, `nc`, `curl`.
- **Developer Tools**: `tcc` (Tiny C Compiler running natively), `kilo` (text editor), `machdemo`, `smpdemo`.
- **GUI Subsystem**: `WindowServer` compositor, `Dock`, `SystemUIServer`, `Filer`, `Terminal`, `calc`.
