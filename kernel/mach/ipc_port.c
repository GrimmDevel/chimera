/* =============================================================================
 * Chimera Operating System — Mach IPC Port Implementation
 * kernel/mach/ipc_port.c
 * ============================================================================= */

#include <kernel/ipc_port.h>
#include <kernel/proc.h>
#include <kernel/ipc_message.h>
#include <kernel/panic.h>

struct ipc_port *ipc_port_kernel_bootstrap = nullptr;

#define IPC_PORT_ARENA_SIZE  4096
static ipc_port_struct_t  s_port_arena[IPC_PORT_ARENA_SIZE];
static u32 s_port_arena_next = 0;         // high-water mark of never-used slots
static ipc_port_struct_t *s_port_free_list = nullptr; // recycled slots (via ip_next)
static spinlock_t s_port_arena_lock = SPINLOCK_INIT;

static struct ipc_port *port_arena_alloc(void) {
    irq_flags_t f = spinlock_lock_irqsave(&s_port_arena_lock);
    ipc_port_struct_t *p = s_port_free_list;
    if (p) {
        s_port_free_list = p->ip_next;
        p->ip_next = nullptr;
    } else if (s_port_arena_next < IPC_PORT_ARENA_SIZE) {
        p = &s_port_arena[s_port_arena_next++];
    }
    spinlock_unlock_irqrestore(&s_port_arena_lock, f);
    return p; // nullptr when the arena is genuinely exhausted
}

static void port_arena_free(struct ipc_port *p) {
    if (!p || p < &s_port_arena[0] || p >= &s_port_arena[IPC_PORT_ARENA_SIZE])
        return;
    irq_flags_t f = spinlock_lock_irqsave(&s_port_arena_lock);
    p->ip_next = s_port_free_list;
    s_port_free_list = p;
    spinlock_unlock_irqrestore(&s_port_arena_lock, f);
}

// name table helpers
mach_port_name_t space_alloc_name(ipc_space_t *space) {
    // scan free list first (free-list "next" is smuggled through ie_object)
    if (space->is_free_count > 0 && space->is_free_head != MACH_PORT_NAME_NULL) {
        u32 idx = ipc_name_index(space->is_free_head);
        ipc_entry_t *e = &space->is_table[idx];
        space->is_free_head = (mach_port_name_t)(uptr)space->is_table[ipc_name_index(space->is_free_head)].ie_object;
        space->is_free_count--;
        e->ie_gen++; // new generation invalidates stale names for this index
        return ipc_name_make(e->ie_gen, idx);
    }
    // linear growth: next unused slot; MACH_PORT_NAME_NULL signals exhaustion
    // (caller must handle it — panicking from a syscall is never acceptable)
    if (space->is_table_used >= space->is_table_size) {
        return MACH_PORT_NAME_NULL;
    }
    u32 idx = space->is_table_used++;
    return ipc_name_make(space->is_table[idx].ie_gen, idx);
}

static void space_free_name(ipc_space_t *space, u32 idx) {
    ipc_entry_t *entry = &space->is_table[idx];
    entry->ie_object = (struct ipc_port *)(uptr)space->is_free_head; // chain
    entry->ie_bits   = MACH_PORT_TYPE_NONE;
    entry->ie_urefs  = 0;
    entry->ie_gen++; // stale names referencing (old gen, idx) now fail
    space->is_free_head = (mach_port_name_t)(uptr)ipc_name_make(entry->ie_gen, idx);
    space->is_free_count++;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ipc_port_init_internal — Initialize a raw port object
 * ═══════════════════════════════════════════════════════════════════════════ */
static void ipc_port_init_internal(struct ipc_port *port, const char *label) {
    port->ip_signature    = CHIMERA_IPC_PORT_MAGIC;
    spinlock_init(&port->ip_lock);
    atomic_store(&port->ip_references, 1);
    port->ip_state        = IPC_PORT_STATE_ACTIVE;
    port->ip_srights      = 0;
    port->ip_sorights     = 0;
    port->ip_receiver     = nullptr;
    port->ip_receiver_name= MACH_PORT_NAME_NULL;
    port->ip_nsrequest    = nullptr;
    port->ip_pdrequest    = nullptr;
    port->ip_mscount      = 0;
    port->ip_next         = nullptr;
    port->ip_prev         = nullptr;
    port->ip_label        = label;
    port->ip_create_time  = 0; // todo: read hardware clock

    ipc_mqueue_t *mq      = &port->ip_messages;
    spinlock_init(&mq->imq_lock);
    mq->imq_messages      = nullptr;
    mq->imq_messages_tail = nullptr;
    mq->imq_msgcount      = 0;
    mq->imq_qlimit        = IPC_PORT_MSG_QUEUE_DEFAULT;
    mq->imq_seqno         = 0;
    mq->imq_reserved      = 0;
    wait_queue_init(&mq->imq_send_waiters);
    wait_queue_init(&mq->imq_recv_waiters);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ipc_space_create
 * ═══════════════════════════════════════════════════════════════════════════ */
chimera_error_t ipc_space_create(struct chimera_task *task, ipc_space_t **space_out) {
    CHIMERA_ASSERT(task != nullptr);
    CHIMERA_ASSERT(space_out != nullptr);

    /* Allocate space struct from kernel heap (kalloc not wired yet in early
     * boot, so use a static pool for Stage 1) */
    static ipc_space_t  s_space_pool[256];
    static _Atomic(u32) s_space_next = 0;
    u32 idx = atomic_fetch_add(&s_space_next, 1);
    if (idx >= 256) {
        // pin the counter so a failed allocation can never wrap around
        // and hand out a live slot
        atomic_store(&s_space_next, 256);
        return CHIMERA_ERR_NOMEM;
    }
    ipc_space_t *space = &s_space_pool[idx];

    spinlock_init(&space->is_lock);
    space->is_active     = true;
    space->is_table_size = IPC_SPACE_INITIAL_CAPACITY;
    space->is_table_used = 1;
    space->is_task       = task;
    space->is_free_head  = MACH_PORT_NAME_NULL;
    space->is_free_count = 0;

    static ipc_entry_t  s_entry_pool[256][IPC_SPACE_INITIAL_CAPACITY];
    space->is_table = s_entry_pool[idx];
    for (u32 i = 0; i < IPC_SPACE_INITIAL_CAPACITY; i++) {
        space->is_table[i].ie_object = nullptr;
        space->is_table[i].ie_bits   = MACH_PORT_TYPE_NONE;
        space->is_table[i].ie_urefs  = 0;
    }

    *space_out = space;
    return CHIMERA_SUCCESS;
}

void ipc_space_destroy(ipc_space_t *space) {
    CHIMERA_ASSERT(space != nullptr);
    irq_flags_t f = spinlock_lock_irqsave(&space->is_lock);
    space->is_active = false;

    struct ipc_port *ports_to_destroy[IPC_SPACE_INITIAL_CAPACITY];
    struct ipc_port *ports_to_release[IPC_SPACE_INITIAL_CAPACITY];
    u32 destroy_count = 0;
    u32 release_count = 0;

    // extract live ports before dropping lock to prevent deadlock in ipc_port_destroy
    for (u32 i = 1; i < space->is_table_used; i++) {
        ipc_entry_t *e = &space->is_table[i];
        if (e->ie_bits != MACH_PORT_TYPE_NONE && e->ie_object) {
            if ((e->ie_bits & MACH_PORT_TYPE_RECEIVE) && (e->ie_object->ip_receiver == space)) {
                if (destroy_count < IPC_SPACE_INITIAL_CAPACITY)
                    ports_to_destroy[destroy_count++] = e->ie_object;
            } else {
                if (release_count < IPC_SPACE_INITIAL_CAPACITY)
                    ports_to_release[release_count++] = e->ie_object;
            }
        }
        e->ie_object = nullptr;
        e->ie_bits   = MACH_PORT_TYPE_NONE;
    }
    space->is_table_used = 1;
    spinlock_unlock_irqrestore(&space->is_lock, f);

    for (u32 i = 0; i < destroy_count; i++) {
        ipc_port_destroy(ports_to_destroy[i]);
    }
    for (u32 i = 0; i < release_count; i++) {
        ipc_port_release(ports_to_release[i]);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ipc_port_alloc
 * ═══════════════════════════════════════════════════════════════════════════ */
chimera_error_t ipc_port_alloc(ipc_space_t *space,
                            mach_port_name_t *name_out,
                            const char *label) {
    CHIMERA_ASSERT(space != nullptr);
    CHIMERA_ASSERT(name_out != nullptr);

    struct ipc_port *port = port_arena_alloc();
    if (!port)
        return CHIMERA_ERR_NOMEM;
    ipc_port_init_internal(port, label);

    irq_flags_t f = spinlock_lock_irqsave(&space->is_lock);

    if (!space->is_active) {
        spinlock_unlock_irqrestore(&space->is_lock, f);
        port_arena_free(port);
        return CHIMERA_ERR_INVALID;
    }

    mach_port_name_t name = space_alloc_name(space);
    if (name == MACH_PORT_NAME_NULL) {
        spinlock_unlock_irqrestore(&space->is_lock, f);
        port_arena_free(port);
        return CHIMERA_ERR_NOMEM;
    }
    ipc_entry_t *entry    = &space->is_table[ipc_name_index(name)];
    entry->ie_object = port;
    entry->ie_bits   = MACH_PORT_TYPE_SEND_RECEIVE;
    entry->ie_urefs  = 1;

    port->ip_receiver      = space;
    port->ip_receiver_name = name;
    port->ip_srights       = 1; // caller holds one send right

    spinlock_unlock_irqrestore(&space->is_lock, f);

    *name_out = name;
    return CHIMERA_SUCCESS;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ipc_port_destroy
 * ═══════════════════════════════════════════════════════════════════════════ */
void ipc_port_destroy(struct ipc_port *port) {
    CHIMERA_ASSERT(port != nullptr);

    irq_flags_t f = spinlock_lock_irqsave(&port->ip_lock);

    if (port->ip_state != IPC_PORT_STATE_ACTIVE) {
        spinlock_unlock_irqrestore(&port->ip_lock, f);
        return; // already dead
    }
    port->ip_state = IPC_PORT_STATE_ZOMBIE;

    // drain message queue
    ipc_mqueue_t *mq = &port->ip_messages;
    ipc_kmsg_t *kmsg  = mq->imq_messages;
    while (kmsg) {
        ipc_kmsg_t *next = kmsg->ikm_next;
        ipc_kmsg_free(kmsg);
        kmsg = next;
    }
    mq->imq_messages      = nullptr;
    mq->imq_messages_tail = nullptr;
    mq->imq_msgcount      = 0;

    // remove from receiver's space
    ipc_space_t *space = port->ip_receiver;
    if (space) {
        irq_flags_t fs = spinlock_lock_irqsave(&space->is_lock);
        mach_port_name_t name = port->ip_receiver_name;
        if (name != MACH_PORT_NAME_NULL &&
            ipc_name_index(name) < space->is_table_size &&
            ipc_name_gen(name) ==
                space->is_table[ipc_name_index(name)].ie_gen) {
            space_free_name(space, ipc_name_index(name));
        }
        spinlock_unlock_irqrestore(&space->is_lock, fs);
        port->ip_receiver      = nullptr;
        port->ip_receiver_name = MACH_PORT_NAME_NULL;
    }

    port->ip_state = IPC_PORT_STATE_DEAD;
    spinlock_unlock_irqrestore(&port->ip_lock, f);

    // wake blocked waiters with an error: receivers and senders re-check
    // port state after waking and return CHIMERA_ERR_PORT_DEAD
    wait_queue_wakeup_one(&mq->imq_recv_waiters);
    wait_queue_wakeup_all(&mq->imq_send_waiters);

    ipc_port_release(port);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ipc_port_lookup — Resolve a name in a space safely
 * ═══════════════════════════════════════════════════════════════════════════ */
struct ipc_port *ipc_port_lookup(ipc_space_t *space,
                             mach_port_name_t name,
                             mach_port_type_t required_right) {
    if (!space || name == MACH_PORT_NAME_NULL || name == MACH_PORT_NAME_DEAD)
        return nullptr;

    u32 idx = ipc_name_index(name);
    if (idx >= space->is_table_size || idx >= space->is_table_used)
        return nullptr;

    irq_flags_t f = spinlock_lock_irqsave(&space->is_lock);

    if (!space->is_active) {
        spinlock_unlock_irqrestore(&space->is_lock, f);
        return nullptr;
    }

    ipc_entry_t *entry = &space->is_table[idx];
    // stale generation => the name belonged to a since-freed right
    if (entry->ie_bits == MACH_PORT_TYPE_NONE || !entry->ie_object ||
        ipc_name_gen(name) != entry->ie_gen) {
        spinlock_unlock_irqrestore(&space->is_lock, f);
        return nullptr;
    }
    // enforce the requested right: a SEND-only name must not receive, etc.
    if (required_right != 0 && !(entry->ie_bits & required_right)) {
        spinlock_unlock_irqrestore(&space->is_lock, f);
        return nullptr;
    }

    struct ipc_port *port = entry->ie_object;
    spinlock_unlock_irqrestore(&space->is_lock, f);

    if (!ipc_port_is_active(port)) {
        return nullptr;
    }
    return port;
}

void ipc_port_unlock(struct ipc_port *port) {
    (void)port;
}

// reference counting
void ipc_port_reference(struct ipc_port *port) {
    CHIMERA_ASSERT(port != nullptr);
    atomic_fetch_add_explicit(&port->ip_references, 1, memory_order_relaxed);
}

void ipc_port_release(struct ipc_port *port) {
    CHIMERA_ASSERT(port != nullptr);
    u32 prev = atomic_fetch_sub_explicit(&port->ip_references, 1,
                                         memory_order_acq_rel);
    if (prev == 1) {
        port->ip_signature = 0xDEADDEADDEADDEADULL;
        // recycle the slot: without this the arena would leak one port per
        // allocation until an avoidable exhaustion
        port_arena_free(port);
    }
}

#define MAX_SERVICES 16
static struct {
    char name[64];
    struct ipc_port *port;
} s_services[MAX_SERVICES];
static u32 s_service_count = 0;
static spinlock_t s_services_lock = SPINLOCK_INIT;

chimera_error_t mach_register_service(const char *name, struct ipc_port *port) {
    irq_flags_t f = spinlock_lock_irqsave(&s_services_lock);
    if (s_service_count >= MAX_SERVICES) {
        spinlock_unlock_irqrestore(&s_services_lock, f);
        return CHIMERA_ERR_OVERFLOW;
    }
    
    // check if already exists
    for (u32 i = 0; i < s_service_count; i++) {
        if (__builtin_strcmp(s_services[i].name, name) == 0) {
            s_services[i].port = port;
            spinlock_unlock_irqrestore(&s_services_lock, f);
            return CHIMERA_SUCCESS;
        }
    }
    
    __builtin_strncpy(s_services[s_service_count].name, name, 64);
    s_services[s_service_count].name[63] = '\0';
    s_services[s_service_count].port = port;
    s_service_count++;
    spinlock_unlock_irqrestore(&s_services_lock, f);
    return CHIMERA_SUCCESS;
}

struct ipc_port *mach_lookup_service(const char *name) {
    irq_flags_t f = spinlock_lock_irqsave(&s_services_lock);
    for (u32 i = 0; i < s_service_count; i++) {
        if (__builtin_strcmp(s_services[i].name, name) == 0) {
            struct ipc_port *p = s_services[i].port;
            spinlock_unlock_irqrestore(&s_services_lock, f);
            return p;
        }
    }
    spinlock_unlock_irqrestore(&s_services_lock, f);
    return nullptr;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ipc_init — Called from chimera_kernel_main Phase 4
 * ═══════════════════════════════════════════════════════════════════════════ */
void ipc_init(void) {
    CHIMERA_ASSERT(task_kernel != nullptr);

    // create kernel task's IPC space
    chimera_error_t err = ipc_space_create(task_kernel, &task_kernel->ta_ipc_space);
    CHIMERA_ASSERT(CHIMERA_SUCCEEDED(err));

    // allocate the kernel bootstrap port
    mach_port_name_t name;
    err = ipc_port_alloc(task_kernel->ta_ipc_space, &name, "kernel.bootstrap");
    CHIMERA_ASSERT(CHIMERA_SUCCEEDED(err));

    ipc_port_kernel_bootstrap =
        task_kernel->ta_ipc_space->is_table[ipc_name_index(name)].ie_object;
    CHIMERA_ASSERT(ipc_port_kernel_bootstrap != nullptr);

    // the bootstrap port is special — pin it
    ipc_port_kernel_bootstrap->ip_state = IPC_PORT_STATE_SPECIAL;

    kprintf("        bootstrap    : port name=0x%llx  ptr=%p\n",
            (unsigned long long)name,
            (void *)ipc_port_kernel_bootstrap);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ipc_port_copyout_send
 * Translates a kernel port pointer to a name in the target space.
 * ═══════════════════════════════════════════════════════════════════════════ */
mach_port_name_t ipc_port_copyout_send(ipc_space_t *space, struct ipc_port *port) {
    if (!port || !space) return MACH_PORT_NAME_NULL;

    irq_flags_t f = spinlock_lock_irqsave(&space->is_lock);
    
    // 1. receiver fastpath
    if (port->ip_receiver == space) {
        mach_port_name_t name = port->ip_receiver_name;
        spinlock_unlock_irqrestore(&space->is_lock, f);
        return name;
    }

    // 2. Does this task already have a send right to this port?
    for (u32 i = 1; i < space->is_table_used; i++) {
        ipc_entry_t *entry = &space->is_table[i];
        if (entry->ie_object == port && (entry->ie_bits & MACH_PORT_TYPE_SEND)) {
            entry->ie_urefs++;
            spinlock_unlock_irqrestore(&space->is_lock, f);
            return (mach_port_name_t)i;
        }
    }

    // 3. Allocate a new name for the send right
    mach_port_name_t name = space_alloc_name(space);
    if (name == MACH_PORT_NAME_NULL) {
        spinlock_unlock_irqrestore(&space->is_lock, f);
        return MACH_PORT_NAME_NULL;
    }
    ipc_entry_t *entry = &space->is_table[ipc_name_index(name)];
    entry->ie_object = port;
    entry->ie_bits   = MACH_PORT_TYPE_SEND;
    entry->ie_urefs  = 1;
    
    // increment port's send right count
    spinlock_lock(&port->ip_lock);
    port->ip_srights++;
    spinlock_unlock(&port->ip_lock);

    spinlock_unlock_irqrestore(&space->is_lock, f);
    return name;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Mach Port System Interfaces (Darwin Mach Trap API)
 * ═══════════════════════════════════════════════════════════════════════════ */

chimera_error_t mach_port_allocate_kernel(ipc_space_t *space, mach_port_right_t right, mach_port_name_t *name_out) {
    if (!space || !name_out) return CHIMERA_ERR_INVALID;

    if (right == MACH_PORT_RIGHT_RECEIVE) {
        return ipc_port_alloc(space, name_out, "mach.port");
    } else if (right == MACH_PORT_RIGHT_PORT_SET) {
        irq_flags_t f = spinlock_lock_irqsave(&space->is_lock);
        mach_port_name_t name = space_alloc_name(space);
        if (name == MACH_PORT_NAME_NULL) {
            spinlock_unlock_irqrestore(&space->is_lock, f);
            return CHIMERA_ERR_NOMEM;
        }
        ipc_entry_t *entry = &space->is_table[ipc_name_index(name)];
        entry->ie_object = nullptr;
        entry->ie_bits = MACH_PORT_TYPE_PORT_SET;
        entry->ie_urefs = 1;
        spinlock_unlock_irqrestore(&space->is_lock, f);
        *name_out = name;
        return CHIMERA_SUCCESS;
    } else if (right == MACH_PORT_RIGHT_DEAD_NAME) {
        irq_flags_t f = spinlock_lock_irqsave(&space->is_lock);
        mach_port_name_t name = space_alloc_name(space);
        if (name == MACH_PORT_NAME_NULL) {
            spinlock_unlock_irqrestore(&space->is_lock, f);
            return CHIMERA_ERR_NOMEM;
        }
        ipc_entry_t *entry = &space->is_table[ipc_name_index(name)];
        entry->ie_object = nullptr;
        entry->ie_bits = MACH_PORT_TYPE_DEAD_NAME;
        entry->ie_urefs = 1;
        spinlock_unlock_irqrestore(&space->is_lock, f);
        *name_out = name;
        return CHIMERA_SUCCESS;
    }

    return CHIMERA_ERR_NOT_SUPPORTED;
}

chimera_error_t mach_port_deallocate_kernel(ipc_space_t *space, mach_port_name_t name) {
    if (!space || name == MACH_PORT_NAME_NULL || name == MACH_PORT_NAME_DEAD) return CHIMERA_ERR_INVALID;

    irq_flags_t f = spinlock_lock_irqsave(&space->is_lock);
    u32 idx = ipc_name_index(name);
    if (idx >= space->is_table_used ||
        ipc_name_gen(name) != space->is_table[idx].ie_gen) {
        spinlock_unlock_irqrestore(&space->is_lock, f);
        return CHIMERA_ERR_INVALID; // stale or forged name
    }

    ipc_entry_t *entry = &space->is_table[idx];
    if (entry->ie_bits == MACH_PORT_TYPE_NONE || entry->ie_urefs == 0) {
        spinlock_unlock_irqrestore(&space->is_lock, f);
        return CHIMERA_ERR_INVALID;
    }

    entry->ie_urefs--;
    if (entry->ie_urefs == 0) {
        struct ipc_port *port = entry->ie_object;
        mach_port_type_t bits = entry->ie_bits;

        space_free_name(space, idx);
        spinlock_unlock_irqrestore(&space->is_lock, f);

        if (port) {
            if ((bits & MACH_PORT_TYPE_RECEIVE) && (port->ip_receiver == space)) {
                ipc_port_destroy(port);
            } else if (bits & MACH_PORT_TYPE_SEND) {
                spinlock_lock(&port->ip_lock);
                if (port->ip_srights > 0) port->ip_srights--;
                spinlock_unlock(&port->ip_lock);
                ipc_port_release(port);
            } else {
                ipc_port_release(port);
            }
        }
        return CHIMERA_SUCCESS;
    }

    spinlock_unlock_irqrestore(&space->is_lock, f);
    return CHIMERA_SUCCESS;
}

chimera_error_t mach_port_type_kernel(ipc_space_t *space, mach_port_name_t name, mach_port_type_t *ptype) {
    if (!space || !ptype || name == MACH_PORT_NAME_NULL) return CHIMERA_ERR_INVALID;

    irq_flags_t f = spinlock_lock_irqsave(&space->is_lock);
    u32 idx = ipc_name_index(name);
    if (idx >= space->is_table_used ||
        ipc_name_gen(name) != space->is_table[idx].ie_gen) {
        spinlock_unlock_irqrestore(&space->is_lock, f);
        return CHIMERA_ERR_INVALID; // stale or forged name
    }

    ipc_entry_t *entry = &space->is_table[idx];
    *ptype = entry->ie_bits;
    spinlock_unlock_irqrestore(&space->is_lock, f);
    return CHIMERA_SUCCESS;
}

