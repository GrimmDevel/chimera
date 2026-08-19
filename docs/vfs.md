# virtual file system (vfs)

darwin/bsd vnode-based filesystem layer.

## vnode architecture
all filesystem objects (files, directories, devices, sockets, pipes) are represented as `vnode_t`:
- `v_type`: `VREG`, `VDIR`, `VBLK`, `VCHR`, `VLNK`, `VSOCK`, `VFIFO`
- `v_usecount`: active reference count
- `v_op`: table of operations (`vnode_op_vtable_t`) implementing open, close, read, write, ioctl, lookup, readdir, create, mkdir, rmdir

## devfs (`/dev`)
in-memory device filesystem mounted at `/dev`:
- `/dev/null`: writes discarded, reads return 0 (EOF)
- `/dev/serial`: direct com1 16550 uart access
- `/dev/console`: canonical line discipline + raw mode text console
- `/dev/mouse`: packet stream of `xiu_event_t` mouse move/click events
- `/dev/fb0`: mmap target for linear framebuffer video memory
- `/dev/ptmx` / `/dev/pts/N`: unix98 pseudoterminal pairs for terminal emulators
- `/dev/disk0`: raw ata hard drive access

## fat32 implementation
persistent filesystem driver on ata/ide block device:
- parses boot sector / bpb, cluster size, root directory cluster
- fat table lookup for cluster chain walking
- supports 8.3 filenames and fat32 long file name (lfn) directory entries
- maps `/bin`, `/Users`, `/Library` on persistent disk

## pipes & pty
- `pipe(fds)` creates unidirectional circular buffer with reader and writer `fileproc_t` entries.
- writing to closed pipe returns `-EPIPE` and delivers `SIGPIPE`.
- `pty_open()` allocates master (`/dev/ptmx`) and slave (`/dev/pts/N`) endpoints for interactive shells and editors.
