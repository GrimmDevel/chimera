# Virtual File System (VFS)

Darwin/BSD vnode-based filesystem layer supporting in-memory devices, persistent FAT32 volumes, pipes, and pseudoterminals.

## Vnode Architecture
Every filesystem entity (files, directories, devices, sockets, pipes) is represented as a `vnode_t`:
- `v_type`: `VREG` (regular file), `VDIR` (directory), `VBLK` (block device), `VCHR` (character device), `VLNK` (symbolic link), `VSOCK` (socket), `VFIFO` (pipe).
- `v_usecount`: Active reference count for inode lifecycle management.
- `v_op`: Operations table (`vnode_op_vtable_t`) implementing `open`, `close`, `read`, `write`, `ioctl`, `lookup`, `readdir`, `create`, `mkdir`, and `rmdir`.

## DevFS (`/dev`)
In-memory device filesystem providing Unix standard character and block device nodes:
- `/dev/null`: Discards all writes; reads immediately return 0 (EOF).
- `/dev/serial`: Direct I/O access to the 16550 UART COM1 serial port.
- `/dev/console`: Canonical line-discipline and raw-mode text console.
- `/dev/mouse`: Stream of mouse movement and click events (`chimera_event_t`).
- `/dev/fb0`: Linear framebuffer device exposing video RAM mappings.
- `/dev/ptmx` / `/dev/pts/N`: Unix98 pseudoterminal master/slave multiplexers for terminal emulators.
- `/dev/disk0`: Raw block device interface for the primary ATA hard drive.

## FAT32 Storage Driver
Persistent storage implementation backed by the ATA/IDE controller:
- Parses BPB (BIOS Parameter Block), cluster sizes, and root directory cluster.
- Walks FAT allocation tables for cluster chain traversal and free cluster allocation.
- Full support for standard 8.3 filenames and Long File Names (LFN).
- Mounts persistent Darwin hierarchy (`/Applications`, `/System`, `/Library`, `/Users`, `/bin`, `/private/etc`).

## Pipes and Pseudoterminals (PTY)
- `pipe(fds)`: Creates a unidirectional circular buffer with reader and writer file handles.
- Enforces buffer invariant checks and atomic read/write limits (`PIPE_BUF`).
- `pty_open()`: Dynamically pairs master endpoints with slave pseudo-terminal devices for shells (`zsh`, `sh`) and full-screen terminal tools (`kilo`).
