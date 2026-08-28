// mach ipc ports and spaces
#pragma once
#ifndef CHIMERA_IPC_PORT_H
#define CHIMERA_IPC_PORT_H

#include <kernel/chimera_types.h>
#include <kernel/spinlock.h>

struct ipc_port;
struct ipc_space;
struct ipc_kmsg;
struct ipc_entry;
struct chimera_task;
struct chimera_thread;

#include <mach/port.h>

typedef chimera_port_name_t     mach_port_name_t;
typedef u32                 mach_port_seqno_t;

#define MACH_PORT_NAME_NULL     CHIMERA_PORT_NULL
#define MACH_PORT_NAME_DEAD     CHIMERA_PORT_DEAD

#define IPC_PORT_MSG_QUEUE_DEFAULT  64
#define IPC_PORT_MSG_QUEUE_MAX      256

#include <kernel/wait_queue.h>

typedef struct ipc_mqueue {
    spinlock_t          imq_lock;
    struct ipc_kmsg    *imq_messages;
    struct ipc_kmsg    *imq_messages_tail;
    u32                 imq_msgcount;
    u32                 imq_qlimit;
    u32                 imq_seqno;
    u32                 imq_reserved;

    wait_queue_t        imq_send_waiters;
    wait_queue_t        imq_recv_waiters;
} ipc_mqueue_t;

typedef enum CHIMERA_PACKED {
    IPC_PORT_STATE_ACTIVE  = 0x01,
    IPC_PORT_STATE_DEAD    = 0x02,
    IPC_PORT_STATE_ZOMBIE  = 0x04,
    IPC_PORT_STATE_SPECIAL = 0x08,
} ipc_port_state_t;

typedef struct CHIMERA_ALIGNED(64) ipc_port {
    u64                 ip_signature;
    spinlock_t          ip_lock;
    atomic_uint         ip_references;
    ipc_port_state_t    ip_state;

    u32                 ip_srights;
    u32                 ip_sorights;

    struct ipc_space   *ip_receiver;
    mach_port_name_t    ip_receiver_name;

    ipc_mqueue_t        ip_messages;

    struct ipc_port    *ip_nsrequest;
    struct ipc_port    *ip_pdrequest;
    mach_port_seqno_t   ip_mscount;

    struct ipc_port    *ip_next;
    struct ipc_port    *ip_prev;

    void               *ip_kobject;
    u32                 ip_kotype;

    const char         *ip_label;
    u64                 ip_create_time;
} ipc_port_struct_t;

#define CHIMERA_IPC_PORT_MAGIC  UINT64_C(0x584955504F525421)

typedef struct ipc_entry {
    struct ipc_port    *ie_object;
    mach_port_type_t    ie_bits;
    u32                 ie_urefs;
} ipc_entry_t;

#define IPC_SPACE_INITIAL_CAPACITY  64
#define IPC_SPACE_MAX_CAPACITY      (1u << 20)

typedef struct ipc_space {
    spinlock_t          is_lock;
    bool                is_active;
    u32                 is_table_size;
    u32                 is_table_used;
    ipc_entry_t        *is_table;
    struct chimera_task    *is_task;

    mach_port_name_t    is_free_head;
    u32                 is_free_count;
} ipc_space_t;

CHIMERA_WARN_UNUSED
chimera_error_t ipc_port_alloc(ipc_space_t *space,
                            mach_port_name_t *name_out,
                            const char *label);

mach_port_name_t ipc_port_copyout_send(ipc_space_t *space, struct ipc_port *port);

void ipc_port_destroy(struct ipc_port *port);

CHIMERA_WARN_UNUSED
struct ipc_port *ipc_port_lookup(ipc_space_t *space,
                             mach_port_name_t name,
                             mach_port_type_t required_right);

void ipc_port_unlock(struct ipc_port *port);

void ipc_port_reference(struct ipc_port *port);
void ipc_port_release(struct ipc_port *port);

CHIMERA_WARN_UNUSED chimera_error_t ipc_space_create(struct chimera_task *task,
                                              ipc_space_t **space_out);
void                         ipc_space_destroy(ipc_space_t *space);

extern struct ipc_port *ipc_port_kernel_bootstrap;

CHIMERA_ALWAYS_INLINE bool ipc_port_is_active(const struct ipc_port *port) {
    return port->ip_state == IPC_PORT_STATE_ACTIVE;
}

CHIMERA_ALWAYS_INLINE bool ipc_port_is_dead(const struct ipc_port *port) {
    return port->ip_state == IPC_PORT_STATE_DEAD;
}

CHIMERA_ALWAYS_INLINE u32 ipc_port_msgcount(const struct ipc_port *port) {
    return port->ip_messages.imq_msgcount;
}

#endif
