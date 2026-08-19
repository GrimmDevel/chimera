# userspace and libsystem

darwin/posix userspace personality, standard c library, and userland utilities.

## libsystem (`usr/libsystem`)
`libsystem` provides standard posix and mach c apis:
- **syscall stubs**: assembly wrappers invoking x86_64 `syscall` instruction.
- **mach ipc api**: `mach_msg()`, `mach_port_allocate()`, `mach_port_insert_right()`, `mach_vm_allocate()`, `mach_vm_deallocate()`, `task_get_bootstrap_port()`.
- **heap allocator**: chunk-based `malloc()`, `free()`, `realloc()`, `calloc()` backed by `mmap()` pages.
- **stdio & string**: full `printf()`, `snprintf()`, `puts()`, `strtok()`, `strncpy()`, `memcmp()`.
- **crt0**: startup entry point initializing environ, arguments, and calling `main(argc, argv)`.

## binary loader
`kernel/bsd/mach_loader.c` executes userspace binaries:
- parses elf64 / mach-o 64-bit headers
- creates user `vm_map` with page-aligned segments (`PT_LOAD` / `LC_SEGMENT_64`)
- allocates user stack (`0x00007FFFFFFFE000`)
- pushes `argc`, `argv`, `envp` onto stack before switching to user entry point via `sysretq` / `task_switch_to_user()`

## process lifecycle
- `fork()`: clones sender `task` and `vm_map` using copy-on-write page table mappings.
- `execve(path, argv, envp)`: replaces address space with new binary image.
- `exit(code)`: terminates process, cleans up fd table, reclaims vm_map pages, notifies parent via `SIGCHLD`.
- `waitpid(pid, status, options)`: reaps zombie process exit status.

## system utilities
- **shells**:
  - `sh`: interactive console shell with command parsing, pipelines (`|`), file redirection (`>`), background execution (`&`).
  - `dash`: netbsd/debian almquist posix shell.
- **core utilities**:
  - `ls`, `cat`, `touch`, `mkdir`, `rm`, `echo`, `pwd`, `neofetch`
- **network tools**:
  - `ping`: icmp echo request utility
  - `ifconfig`: displays network interfaces, ip, netmask, mac addresses
  - `nc`: netcat utility for raw tcp/udp connections and port testing
  - `curl`: http get client for web requests
- **gui subsystem**:
  - `wserver`: window server compositor managing window buffers, cursor blending, and event routing via mach ipc.
  - `guiapp`: activity monitor gui application.
  - `calc`: macos-style gui calculator.
- **developer tools**:
  - `tcc`: tiny c compiler running natively inside xiu.
  - `kilo`: full-screen terminal text editor.
  - `smpdemo`: multi-threaded parallel execution test across all cpu cores.
  - `machdemo`: mach port communication and ool memory sharing demonstration.
