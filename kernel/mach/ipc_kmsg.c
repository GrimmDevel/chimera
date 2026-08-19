/* =============================================================================
 * XIU Operating System — IPC Kernel Message Implementation
 * kernel/mach/ipc_kmsg.c
 * =============================================================================
 */

#include <kernel/ipc_message.h>
#include <kernel/ipc_port.h>
#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/input.h>
#include <kernel/xiu_types.h>

#define KMSG_POOL_SIZE 2048
#define KMSG_INLINE_MAX 512

typedef struct kmsg_pool_entry {
  u8 buffer[sizeof(ipc_kmsg_t) + KMSG_INLINE_MAX];
} kmsg_pool_entry_t;

static kmsg_pool_entry_t s_kmsg_pool[KMSG_POOL_SIZE];
static _Atomic(u32) s_kmsg_next = 0;

ipc_kmsg_t *ipc_kmsg_alloc(mach_msg_size_t msg_size) {
  if (msg_size > MACH_MSG_SIZE_MAX) {
    xiu_panic("ipc_kmsg_alloc: message too large (%u > %u)\n", msg_size,
              MACH_MSG_SIZE_MAX);
  }

  u32 idx = atomic_fetch_add(&s_kmsg_next, 1) % KMSG_POOL_SIZE;
  kmsg_pool_entry_t *e = &s_kmsg_pool[idx];
  ipc_kmsg_t *kmsg = (ipc_kmsg_t *)e->buffer;

  // zero the header portion
  __builtin_memset(kmsg, 0, sizeof(ipc_kmsg_t));
  kmsg->ikm_size = KMSG_INLINE_MAX;
  kmsg->ikm_header_offset = 0;
  kmsg->ikm_header = (mach_msg_header_t *)kmsg->ikm_data;
  kmsg->ikm_next = nullptr;
  kmsg->ikm_prev = nullptr;
  kmsg->ikm_ool_count = 0;

  return kmsg;
}

void ipc_kmsg_free(ipc_kmsg_t *kmsg) {
  // stage 1: pool entries are not freed — just poisoned
  if (kmsg) {
    kmsg->ikm_header = nullptr;
  }
}

extern xiu_error_t copyin(const void *uaddr, void *kaddr, usize len);
extern xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);
extern xiu_paddr_t pmm_alloc_page(void);
extern void pmm_release_page(xiu_paddr_t addr);

// copy-in: translate a user message into a kernel message
xiu_error_t ipc_kmsg_copyin(ipc_kmsg_t *kmsg, xiu_vaddr_t user_header_va,
                            ipc_space_t *space) {
  XIU_ASSERT(kmsg != nullptr);
  XIU_ASSERT(space != nullptr);

  if (user_header_va == XIU_VADDR_NULL)
    return XIU_ERR_INVALID;

  mach_msg_header_t user_hdr;
  if (copyin((const void *)user_header_va, &user_hdr, sizeof(user_hdr)) != XIU_SUCCESS) {
    return XIU_ERR_INVALID;
  }

  if (user_hdr.msgh_size < MACH_MSG_HEADER_SIZE ||
      user_hdr.msgh_size > MACH_MSG_SIZE_MAX) {
    return XIU_ERR_INVALID;
  }

  // ponytail: copy message safely from user space into kernel message buffer
  mach_msg_header_t *khdr = kmsg->ikm_header;
  if (copyin((const void *)user_header_va, khdr, user_hdr.msgh_size) != XIU_SUCCESS) {
    return XIU_ERR_INVALID;
  }

  kmsg->ikm_sender_pid = (space->is_task && space->is_task->ta_proc)
                             ? space->is_task->ta_proc->p_pid
                             : 0;
  kmsg->ikm_sender_uid = (space->is_task && space->is_task->ta_proc)
                             ? space->is_task->ta_proc->p_uid
                             : 0;

  // translate remote port name → kernel pointer
  mach_port_name_t rname = khdr->msgh_remote_port;
  mach_port_type_t right = MACH_PORT_TYPE_SEND;

  if (MACH_MSGH_BITS_REMOTE(khdr->msgh_bits) == MACH_PORT_RIGHT_SEND_ONCE) {
    right = MACH_PORT_TYPE_SEND_ONCE;
  }

  ipc_port_t *remote = ipc_port_lookup(space, rname, right);
  if (!remote)
    return XIU_ERR_PORT_DEAD;
  kmsg->ikm_remote_port = remote;
  kmsg->ikm_remote_right = right;

  // translate local reply port if present
  mach_port_name_t lname = khdr->msgh_local_port;
  if (lname != MACH_PORT_NAME_NULL) {
    if (lname == rname) {
      kmsg->ikm_local_port = remote;
      kmsg->ikm_local_right = MACH_PORT_TYPE_SEND_RECEIVE;
    } else {
      ipc_port_t *local = ipc_port_lookup(space, lname, MACH_PORT_TYPE_RECEIVE);
      if (local) {
        kmsg->ikm_local_port = local;
        kmsg->ikm_local_right = MACH_PORT_TYPE_RECEIVE;
      }
    }
  }

  // port descriptors and ool memory
  if (khdr->msgh_bits & MACH_MSGH_BITS_COMPLEX) {
    mach_msg_body_t *body = (mach_msg_body_t *)(khdr + 1);
    u8 *desc_ptr = (u8 *)(body + 1);
    u32 desc_count = body->msgh_descriptor_count;

    for (u32 i = 0; i < desc_count && i < 16; i++) {
      mach_msg_type_descriptor_t *td = (mach_msg_type_descriptor_t *)desc_ptr;
      mach_msg_descriptor_type_t dtype = td->type;
      if (dtype == MACH_MSG_PORT_DESCRIPTOR) {
        mach_msg_port_descriptor_t *pdesc = (mach_msg_port_descriptor_t *)desc_ptr;
        ipc_port_t *port_obj = ipc_port_lookup(space, pdesc->name, MACH_PORT_TYPE_SEND);
        if (port_obj) {
          ipc_port_unlock(port_obj);
          pdesc->name = (mach_port_name_t)(uptr)port_obj; // temporarily hold kernel pointer in transit
        } else {
          pdesc->name = MACH_PORT_NAME_NULL;
        }
        desc_ptr += sizeof(mach_msg_port_descriptor_t);
      } else if (dtype == MACH_MSG_OOL_DESCRIPTOR) {
        mach_msg_ool_descriptor_t *ool = (mach_msg_ool_descriptor_t *)desc_ptr;
        if (ool->size > 0 && ool->size <= MACH_MSG_OOL_MAX && ool->address != 0) {
          // allocate kernel pages to hold OOL payload
          usize page_count = (ool->size + 4095) / 4096;
          extern xiu_paddr_t pmm_alloc_pages(usize count);
          xiu_paddr_t paddr = pmm_alloc_pages(page_count);
          if (paddr) {
            void *kbuf = (void *)(paddr + g_hhdm_base);
            copyin((const void *)ool->address, kbuf, ool->size);

            u32 ool_idx = kmsg->ikm_ool_count++;
            kmsg->ikm_ool_regions[ool_idx].addr = (xiu_vaddr_t)(uptr)kbuf;
            kmsg->ikm_ool_regions[ool_idx].size = ool->size;

            ool->address = (xiu_vaddr_t)(uptr)kbuf;
          }
        }
        desc_ptr += sizeof(mach_msg_ool_descriptor_t);
      } else {
        break;
      }
    }
  }

  return XIU_SUCCESS;
}

// copy-out: deliver a kernel message to a user receive buffer
xiu_error_t ipc_kmsg_copyout(ipc_kmsg_t *kmsg, xiu_vaddr_t user_buf_va,
                             mach_msg_size_t buf_size, ipc_space_t *space) {
  XIU_ASSERT(kmsg != nullptr);
  XIU_ASSERT(space != nullptr);

  mach_msg_header_t *khdr = kmsg->ikm_header;
  mach_msg_size_t total_sz = khdr->msgh_size + sizeof(mach_msg_audit_trailer_t);
  if (buf_size < total_sz)
    return XIU_ERR_OVERFLOW;

  u8 temp[2048];
  if (total_sz > sizeof(temp))
    return XIU_ERR_OVERFLOW;

  // ponytail: prepare kernel message + audit trailer in kernel stack and copyout safely
  __builtin_memcpy(temp, khdr, khdr->msgh_size);

  mach_msg_audit_trailer_t *trailer =
      (mach_msg_audit_trailer_t *)(temp + khdr->msgh_size);
  trailer->msgh_trailer_type = MACH_MSG_TRAILER_FORMAT_0;
  trailer->msgh_trailer_size = sizeof(mach_msg_audit_trailer_t);
  trailer->msgh_seqno = kmsg->ikm_seqno;

  mach_msg_header_t *out_hdr = (mach_msg_header_t *)temp;

  // 1. destination of reply
  out_hdr->msgh_remote_port = ipc_port_copyout_send(space, kmsg->ikm_local_port);

  // 2. port message was received on
  if (kmsg->ikm_remote_port && kmsg->ikm_remote_port->ip_receiver == space) {
    out_hdr->msgh_local_port = kmsg->ikm_remote_port->ip_receiver_name;
  } else {
    out_hdr->msgh_local_port = MACH_PORT_NAME_NULL;
  }
  trailer->msgh_sender_pid = kmsg->ikm_sender_pid;
  trailer->msgh_sender_uid = kmsg->ikm_sender_uid;

  // complex message copy-out
  if (out_hdr->msgh_bits & MACH_MSGH_BITS_COMPLEX) {
    mach_msg_body_t *body = (mach_msg_body_t *)(out_hdr + 1);
    u8 *desc_ptr = (u8 *)(body + 1);
    u32 desc_count = body->msgh_descriptor_count;
    u32 ool_idx = 0;

    for (u32 i = 0; i < desc_count && i < 16; i++) {
      mach_msg_type_descriptor_t *td = (mach_msg_type_descriptor_t *)desc_ptr;
      mach_msg_descriptor_type_t dtype = td->type;
      if (dtype == MACH_MSG_PORT_DESCRIPTOR) {
        mach_msg_port_descriptor_t *pdesc = (mach_msg_port_descriptor_t *)desc_ptr;
        ipc_port_t *port_obj = (ipc_port_t *)(uptr)pdesc->name;
        if (port_obj) {
          pdesc->name = ipc_port_copyout_send(space, port_obj);
        } else {
          pdesc->name = MACH_PORT_NAME_NULL;
        }
        desc_ptr += sizeof(mach_msg_port_descriptor_t);
      } else if (dtype == MACH_MSG_OOL_DESCRIPTOR) {
        mach_msg_ool_descriptor_t *ool = (mach_msg_ool_descriptor_t *)desc_ptr;
        if (ool_idx < kmsg->ikm_ool_count) {
          void *kbuf = (void *)(uptr)kmsg->ikm_ool_regions[ool_idx].addr;
          usize ool_sz = kmsg->ikm_ool_regions[ool_idx].size;
          usize page_count = (ool_sz + 4095) / 4096;
          
          // allocate user memory mapping in receiver task for all pages
          xiu_paddr_t user_paddr = (xiu_paddr_t)((uptr)kbuf - g_hhdm_base);
          static u64 s_dyn_ool_vaddr = 0x0000700000000000ULL;
          u64 target_va = s_dyn_ool_vaddr;
          s_dyn_ool_vaddr += page_count * 4096;
          if (s_dyn_ool_vaddr > 0x0000780000000000ULL) s_dyn_ool_vaddr = 0x0000700000000000ULL;

          if (space->is_task && space->is_task->ta_vm_map) {
            extern u64 pmap_map_user_page(u64 target_pml4_phys, u64 vaddr, u64 paddr, u32 flags);
            for (usize pg = 0; pg < page_count; pg++) {
              pmap_map_user_page((u64)space->is_task->ta_vm_map,
                                 target_va + (pg * 4096),
                                 user_paddr + (pg * 4096),
                                 (1ULL << 0) | (1ULL << 1) | (1ULL << 2));
            }
            ool->address = target_va;
          }
          ool_idx++;
        }
        desc_ptr += sizeof(mach_msg_ool_descriptor_t);
      } else {
        break;
      }
    }
  }

  if (copyout(temp, (void *)user_buf_va, total_sz) != XIU_SUCCESS) {
    return XIU_ERR_INVALID;
  }

  return XIU_SUCCESS;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * ipc_mqueue_send — Enqueue kmsg onto a port's message queue
 * ═══════════════════════════════════════════════════════════════════════════
 */
xiu_error_t ipc_mqueue_send(ipc_port_t *port, ipc_kmsg_t *kmsg,
                            mach_msg_timeout_t timeout_ms) {
  XIU_ASSERT(port != nullptr);
  XIU_ASSERT(kmsg != nullptr);
  (void)timeout_ms; // blocking not implemented in Stage 1

  ipc_mqueue_t *mq = &port->ip_messages;
  irq_flags_t f = spinlock_lock_irqsave(&mq->imq_lock);

  if (!ipc_port_is_active(port)) {
    spinlock_unlock_irqrestore(&mq->imq_lock, f);
    return XIU_ERR_PORT_DEAD;
  }

  // kernel object dispatch
  if (port->ip_kobject != nullptr) {
    spinlock_unlock_irqrestore(&mq->imq_lock, f);
    extern xiu_error_t ipc_kobject_server(ipc_port_t *port, ipc_kmsg_t *request_kmsg);
    return ipc_kobject_server(port, kmsg);
  }

  if (mq->imq_msgcount >= mq->imq_qlimit) {
    spinlock_unlock_irqrestore(&mq->imq_lock, f);
    return XIU_ERR_PORT_FULL;
  }

  // assign sequence number
  kmsg->ikm_seqno = mq->imq_seqno++;
  kmsg->ikm_next = nullptr;
  kmsg->ikm_prev = mq->imq_messages_tail;

  if (mq->imq_messages_tail) {
    mq->imq_messages_tail->ikm_next = kmsg;
  } else {
    mq->imq_messages = kmsg; // first message
  }
  mq->imq_messages_tail = kmsg;
  mq->imq_msgcount++;

  // wake one blocked receiver
  wait_queue_wakeup_one(&mq->imq_recv_waiters);
  spinlock_unlock_irqrestore(&mq->imq_lock, f);

  return XIU_SUCCESS;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ipc_mqueue_receive — Dequeue kmsg from a port's message queue
 * ═══════════════════════════════════════════════════════════════════════════
 */
xiu_error_t ipc_mqueue_receive(ipc_port_t *port, ipc_kmsg_t **kmsg_out,
                               mach_msg_timeout_t timeout_ms) {
  XIU_ASSERT(port != nullptr);
  XIU_ASSERT(kmsg_out != nullptr);

  ipc_mqueue_t *mq = &port->ip_messages;
  irq_flags_t f = spinlock_lock_irqsave(&mq->imq_lock);

  for (;;) {
    if (!ipc_port_is_active(port)) {
      spinlock_unlock_irqrestore(&mq->imq_lock, f);
      return XIU_ERR_PORT_DEAD;
    }

    if (mq->imq_msgcount > 0) {
      break;
    }

    // no message — Handle non-blocking or block
    if (timeout_ms == 0) {
      spinlock_unlock_irqrestore(&mq->imq_lock, f);
      return XIU_ERR_TIMEOUT;
    }

    xiu_error_t err =
        wait_queue_sleep_irqrestore(&mq->imq_recv_waiters, &mq->imq_lock, f);
    if (err != XIU_SUCCESS) {
      return err;
    }

    // re-acquire lock to check queue again
    f = spinlock_lock_irqsave(&mq->imq_lock);
  }

  // dequeue head
  ipc_kmsg_t *kmsg = mq->imq_messages;
  mq->imq_messages = kmsg->ikm_next;
  if (mq->imq_messages) {
    mq->imq_messages->ikm_prev = nullptr;
  } else {
    mq->imq_messages_tail = nullptr;
  }
  mq->imq_msgcount--;
  kmsg->ikm_next = nullptr;
  kmsg->ikm_prev = nullptr;

  spinlock_unlock_irqrestore(&mq->imq_lock, f);

  *kmsg_out = kmsg;
  return XIU_SUCCESS;
}

