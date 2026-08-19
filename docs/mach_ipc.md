# mach ipc

darwin mach-style message passing subsystem.

## core concepts
- `ipc_port_t`: kernel-allocated object representing a message queue. Protected by spinlock.
- `ipc_space_t`: per-task table of port names (`mach_port_name_t`).
- `ipc_entry_t`: maps port name to `ipc_port_t *` with specific rights.

## port rights
- `MACH_PORT_RIGHT_SEND`: allows sending messages to port.
- `MACH_PORT_RIGHT_RECEIVE`: only one task holds receive right per port. Allows dequeueing messages.
- `MACH_PORT_RIGHT_SEND_ONCE`: single-use send capability, consumed after sending reply.
- `MACH_PORT_RIGHT_DEAD_NAME`: dead port tombstone after port destroy.

## message format
standard mach header with optional complex body:
```c
typedef struct {
    mach_msg_bits_t       msgh_bits;
    mach_msg_size_t       msgh_size;
    mach_port_t           msgh_remote_port;
    mach_port_t           msgh_local_port;
    mach_port_name_t      msgh_voucher_port;
    mach_msg_id_t         msgh_id;
} mach_msg_header_t;
```

## out-of-line (ool) memory
for large data transfers (e.g. window server compositor surfaces):
- `mach_msg_ool_descriptor_t` passes virtual address and size.
- kernel maps physical pages from sender `vm_map` into receiver `vm_map` using copy-on-write page table flags.
- zero-copy page sharing instead of large memcpy in ipc queues.

## traps / syscalls
- `mach_msg_trap(msg, option, send_size, rcv_size, rcv_name, timeout, notify)`
- `mach_port_allocate(task, right, &name)`
- `mach_port_insert_right(task, name, port, right_type)`
- `mach_port_deallocate(task, name)`
- `mach_reply_port()` - allocates or reuses thread-local send-once reply port
- `task_get_bootstrap_port(task, &port)` - retrieves system service lookup port
