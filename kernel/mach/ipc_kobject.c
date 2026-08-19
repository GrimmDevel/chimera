// mach kernel objects and mig rpc server dispatcher
#include <kernel/ipc_kobject.h>
#include <kernel/ipc_message.h>
#include <kernel/ipc_port.h>
#include <kernel/proc.h>
#include <kernel/panic.h>
#include <kernel/xiu_types.h>

extern void kprintf(const char *fmt, ...);
extern xiu_paddr_t pmm_alloc_page(void);
extern void pmm_release_page(xiu_paddr_t addr);
extern u64 pmap_map_user_page(u64 target_pml4_phys, u64 vaddr, u64 paddr, u32 flags);
extern void pmap_unmap_user_range(u64 pml4_phys, u64 vaddr, usize len);

void ipc_kobject_set(ipc_port_t *port, void *kobject, ipc_kobject_type_t type) {
    if (!port) return;
    irq_flags_t f = spinlock_lock_irqsave(&port->ip_lock);
    port->ip_kobject = kobject;
    port->ip_kotype  = type;
    spinlock_unlock_irqrestore(&port->ip_lock, f);
}

static void send_mig_reply_ret(ipc_port_t *reply_port, mach_msg_id_t reply_id, i32 ret_code) {
    if (!reply_port) return;

    typedef struct XIU_PACKED {
        mach_msg_header_t hdr;
        u32               ret_code;
    } mig_reply_msg_t;

    mig_reply_msg_t rep;
    __builtin_memset(&rep, 0, sizeof(rep));
    rep.hdr.msgh_bits = 0;
    rep.hdr.msgh_size = sizeof(rep);
    rep.hdr.msgh_remote_port = (mach_port_name_t)(uptr)reply_port;
    rep.hdr.msgh_local_port = MACH_PORT_NAME_NULL;
    rep.hdr.msgh_id = reply_id;
    rep.ret_code = (u32)ret_code;

    ipc_kmsg_t *kmsg = ipc_kmsg_alloc(sizeof(rep));
    if (kmsg) {
        __builtin_memcpy(kmsg->ikm_header, &rep, sizeof(rep));
        kmsg->ikm_remote_port = reply_port;
        ipc_mqueue_send(reply_port, kmsg, 0);
    }
}

xiu_error_t ipc_kobject_server(ipc_port_t *port, ipc_kmsg_t *kmsg) {
    if (!port || !kmsg) return XIU_ERR_INVALID;

    mach_msg_header_t *hdr = kmsg->ikm_header;
    mach_msg_id_t msg_id = hdr->msgh_id;
    ipc_port_t *reply_port = kmsg->ikm_local_port;
    u8 *req_data = (u8 *)(hdr + 1);

    if (port->ip_kotype == IKOT_TASK) {
        xiu_task_t *target_task = (xiu_task_t *)port->ip_kobject;
        if (!target_task) target_task = current_task();

        switch (msg_id) {
            case MACH_VM_ALLOCATE_ID: {
                typedef struct XIU_PACKED {
                    u64 address;
                    u64 size;
                    u32 flags;
                } req_vm_alloc_t;

                req_vm_alloc_t *req = (req_vm_alloc_t *)req_data;
                u64 req_size = (req->size + 4095) & ~4095ULL;
                u64 target_va = req->address;

                if (target_va == 0 || (req->flags & 1)) {
                    static u64 s_next_mmap_va = 0x0000600000000000ULL;
                    target_va = s_next_mmap_va;
                    s_next_mmap_va += req_size + 4096;
                    if (s_next_mmap_va > 0x00006f0000000000ULL) s_next_mmap_va = 0x0000600000000000ULL;
                }

                if (target_task && target_task->ta_vm_map) {
                    for (u64 offset = 0; offset < req_size; offset += 4096) {
                        xiu_paddr_t paddr = pmm_alloc_page();
                        if (paddr) {
                            void *page = (void *)(paddr + HHDM_BASE);
                            __builtin_memset(page, 0, 4096);
                            pmap_map_user_page((u64)target_task->ta_vm_map, target_va + offset,
                                               paddr, (1ULL << 0) | (1ULL << 1) | (1ULL << 2));
                        }
                    }
                }

                typedef struct XIU_PACKED {
                    mach_msg_header_t hdr;
                    u32               ret_code;
                    u64               address;
                } rep_vm_alloc_t;

                rep_vm_alloc_t rep;
                __builtin_memset(&rep, 0, sizeof(rep));
                rep.hdr.msgh_size = sizeof(rep);
                rep.hdr.msgh_id = MACH_VM_ALLOCATE_ID + 100;
                rep.ret_code = 0;
                rep.address = target_va;

                if (reply_port) {
                    ipc_kmsg_t *rep_kmsg = ipc_kmsg_alloc(sizeof(rep));
                    if (rep_kmsg) {
                        __builtin_memcpy(rep_kmsg->ikm_header, &rep, sizeof(rep));
                        rep_kmsg->ikm_remote_port = reply_port;
                        ipc_mqueue_send(reply_port, rep_kmsg, 0);
                    }
                }
                ipc_kmsg_free(kmsg);
                return XIU_SUCCESS;
            }

            case MACH_VM_DEALLOCATE_ID: {
                typedef struct XIU_PACKED {
                    u64 address;
                    u64 size;
                } req_vm_dealloc_t;

                req_vm_dealloc_t *req = (req_vm_dealloc_t *)req_data;
                if (target_task && target_task->ta_vm_map && req->size > 0) {
                    pmap_unmap_user_range((u64)target_task->ta_vm_map, req->address, req->size);
                }
                send_mig_reply_ret(reply_port, MACH_VM_DEALLOCATE_ID + 100, 0);
                ipc_kmsg_free(kmsg);
                return XIU_SUCCESS;
            }

            case TASK_GET_SPECIAL_PORT_ID: {
                typedef struct XIU_PACKED {
                    u32 which_port;
                } req_task_get_port_t;

                req_task_get_port_t *req = (req_task_get_port_t *)req_data;
                mach_port_name_t found_name = MACH_PORT_NAME_NULL;

                if (req->which_port == TASK_BOOTSTRAP_PORT) {
                    found_name = target_task->ta_bootstrap_port;
                } else if (req->which_port == TASK_KERNEL_PORT) {
                    found_name = target_task->ta_task_port;
                }

                typedef struct XIU_PACKED {
                    mach_msg_header_t           hdr;
                    mach_msg_body_t             body;
                    mach_msg_port_descriptor_t  port;
                    u32                         ret_code;
                } rep_task_get_port_t;

                rep_task_get_port_t rep;
                __builtin_memset(&rep, 0, sizeof(rep));
                rep.hdr.msgh_bits = MACH_MSGH_BITS_COMPLEX;
                rep.hdr.msgh_size = sizeof(rep);
                rep.hdr.msgh_id = TASK_GET_SPECIAL_PORT_ID + 100;
                rep.body.msgh_descriptor_count = 1;
                rep.port.type = MACH_MSG_PORT_DESCRIPTOR;
                rep.port.name = found_name;
                rep.ret_code = 0;

                if (reply_port) {
                    ipc_kmsg_t *rep_kmsg = ipc_kmsg_alloc(sizeof(rep));
                    if (rep_kmsg) {
                        __builtin_memcpy(rep_kmsg->ikm_header, &rep, sizeof(rep));
                        rep_kmsg->ikm_remote_port = reply_port;
                        ipc_mqueue_send(reply_port, rep_kmsg, 0);
                    }
                }
                ipc_kmsg_free(kmsg);
                return XIU_SUCCESS;
            }

            case TASK_SET_SPECIAL_PORT_ID: {
                typedef struct XIU_PACKED {
                    u32 which_port;
                    mach_port_name_t port_name;
                } req_task_set_port_t;

                req_task_set_port_t *req = (req_task_set_port_t *)req_data;
                if (req->which_port == TASK_BOOTSTRAP_PORT) {
                    target_task->ta_bootstrap_port = req->port_name;
                }
                send_mig_reply_ret(reply_port, TASK_SET_SPECIAL_PORT_ID + 100, 0);
                ipc_kmsg_free(kmsg);
                return XIU_SUCCESS;
            }

            case TASK_INFO_ID: {
                typedef struct XIU_PACKED {
                    mach_msg_header_t hdr;
                    u32               ret_code;
                    u32               pid;
                    u64               virtual_size;
                    u64               resident_size;
                } rep_task_info_t;

                rep_task_info_t rep;
                __builtin_memset(&rep, 0, sizeof(rep));
                rep.hdr.msgh_size = sizeof(rep);
                rep.hdr.msgh_id = TASK_INFO_ID + 100;
                rep.ret_code = 0;
                rep.pid = target_task->ta_proc ? target_task->ta_proc->p_pid : 0;
                rep.virtual_size = 16 * 1024 * 1024;
                rep.resident_size = 64 * 4096;

                if (reply_port) {
                    ipc_kmsg_t *rep_kmsg = ipc_kmsg_alloc(sizeof(rep));
                    if (rep_kmsg) {
                        __builtin_memcpy(rep_kmsg->ikm_header, &rep, sizeof(rep));
                        rep_kmsg->ikm_remote_port = reply_port;
                        ipc_mqueue_send(reply_port, rep_kmsg, 0);
                    }
                }
                ipc_kmsg_free(kmsg);
                return XIU_SUCCESS;
            }

            default:
                break;
        }
    }

    send_mig_reply_ret(reply_port, msg_id + 100, -1);
    ipc_kmsg_free(kmsg);
    return XIU_SUCCESS;
}
