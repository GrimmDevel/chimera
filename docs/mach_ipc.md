# Mach IPC Subsystem

Darwin Mach-style message passing subsystem with capability-based security.

## Core Concepts
- `ipc_port_t`: Kernel-allocated object representing a message queue, protected by `irqsave` spinlocks.
- `ipc_space_t`: Per-task table of port names (`mach_port_name_t`) and rights.
- `ipc_entry_t`: Table entry associating a port name with an `ipc_port_t *` and capability rights.
- `ipc_kmsg_t`: In-flight kernel message envelope containing header, inline body, and optional OOL descriptors.

## Port Rights
- `MACH_PORT_RIGHT_SEND`: Capability to transmit messages to a port.
- `MACH_PORT_RIGHT_RECEIVE`: Capability to dequeue messages from a port. Exactly one task holds receive rights for a given port.
- `MACH_PORT_RIGHT_SEND_ONCE`: Single-use send capability, consumed immediately upon transmitting a reply.
- `MACH_PORT_RIGHT_DEAD_NAME`: Tombstone right signifying the underlying port has been destroyed.

## Message Format
Standard Mach header with optional complex body:
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

## Out-of-Line (OOL) Memory
For large data transfers (such as WindowServer graphics surfaces):
- `mach_msg_ool_descriptor_t` specifies the user virtual address, size, copy method, and deallocate options.
- The kernel maps physical pages from the sender's `vm_map` into the receiver's `vm_map` using copy-on-write (COW) page table attributes.
- Eliminates large `memcpy` operations within kernel message buffers.
- Verified bounds checking: user addresses passed in descriptors are validated against `USER_SPACE_MIN` and `USER_SPACE_MAX`.

## Mach Traps & System Calls
- `mach_msg_trap(msg, option, send_size, rcv_size, rcv_name, timeout, notify)` — primary message transmission and reception trap.
- `mach_port_allocate(task, right, &name)` — allocates a new port right in the task's IPC space.
- `mach_port_insert_right(task, name, port, right_type)` — inserts an existing right into target task space.
- `mach_port_deallocate(task, name)` — drops a reference to a port right.
- `mach_reply_port()` — allocates or reuses a thread-local send-once reply port.
- `task_get_bootstrap_port(task, &port)` — retrieves system service lookup port.
