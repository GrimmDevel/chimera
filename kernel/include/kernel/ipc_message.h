// mach ipc message format and kmsg wrapper
#pragma once
#ifndef XIU_IPC_MESSAGE_H
#define XIU_IPC_MESSAGE_H

#include <kernel/xiu_types.h>
#include <kernel/ipc_port.h>

#define MACH_MSG_SIZE_MAX       (1024 * 1024)
#define MACH_MSG_HEADER_SIZE    24
#define MACH_MSG_TRAILER_SIZE   8
#define MACH_MSG_OOL_MAX        (4 * 1024 * 1024)

typedef u32 mach_msg_bits_t;

#define MACH_MSGH_BITS_REMOTE_MASK      0x0000001f
#define MACH_MSGH_BITS_LOCAL_MASK       0x00001f00
#define MACH_MSGH_BITS_VOUCHER_MASK     0x001f0000
#define MACH_MSGH_BITS_COMPLEX          0x80000000u
#define MACH_MSGH_BITS_RAISEIMP         0x20000000u
#define MACH_MSGH_BITS_DENAP            0x10000000u

#define MACH_MSGH_BITS_REMOTE(bits)  ((bits) & MACH_MSGH_BITS_REMOTE_MASK)
#define MACH_MSGH_BITS_LOCAL(bits)   (((bits) & MACH_MSGH_BITS_LOCAL_MASK)  >> 8)
#define MACH_MSGH_BITS_VOUCHER(bits) (((bits) & MACH_MSGH_BITS_VOUCHER_MASK) >> 16)

#define MACH_MSGH_BITS(remote, local) \
    ((mach_msg_bits_t)(remote) | ((mach_msg_bits_t)(local) << 8))

typedef u32 mach_msg_id_t;
typedef u32 mach_msg_size_t;
typedef u32 mach_msg_timeout_t;

#define MACH_MSG_TIMEOUT_NONE       0u
#define MACH_MSG_TIMEOUT_IMMEDIATE  1u

// header
typedef struct XIU_PACKED mach_msg_header {
    mach_msg_bits_t     msgh_bits;
    mach_msg_size_t     msgh_size;
    mach_port_name_t    msgh_remote_port;
    mach_port_name_t    msgh_local_port;
    mach_port_name_t    msgh_voucher_port;
    mach_msg_id_t       msgh_id;
} mach_msg_header_t;

XIU_STATIC_ASSERT(sizeof(mach_msg_header_t) == MACH_MSG_HEADER_SIZE,
                  "mach_msg_header_t size must be 24 bytes");

typedef struct mach_msg_body {
    u32 msgh_descriptor_count;
} mach_msg_body_t;

typedef u8 mach_msg_descriptor_type_t;
#define MACH_MSG_PORT_DESCRIPTOR        0u
#define MACH_MSG_OOL_DESCRIPTOR         1u
#define MACH_MSG_OOL_PORTS_DESCRIPTOR   2u
#define MACH_MSG_OOL_VOLATILE_DESCRIPTOR 3u

typedef struct XIU_PACKED mach_msg_type_descriptor {
    u32                         pad1;
    u32                         pad2;
    u16                         pad3;
    u8                          pad4;
    mach_msg_descriptor_type_t  type;
} mach_msg_type_descriptor_t;

typedef struct XIU_PACKED mach_msg_port_descriptor {
    mach_port_name_t            name;
    u32                         pad1;
    u16                         pad2;
    u8                          disposition;
    mach_msg_descriptor_type_t  type;
} mach_msg_port_descriptor_t;

typedef struct XIU_PACKED mach_msg_ool_descriptor {
    xiu_vaddr_t                 address;
    u8                          deallocate;
    u8                          copy;
    u8                          pad1;
    mach_msg_descriptor_type_t  type;
    mach_msg_size_t             size;
} mach_msg_ool_descriptor_t;

XIU_STATIC_ASSERT(sizeof(mach_msg_type_descriptor_t) == 12, "type desc size 12");
XIU_STATIC_ASSERT(sizeof(mach_msg_port_descriptor_t) == 12, "port desc size 12");
XIU_STATIC_ASSERT(sizeof(mach_msg_ool_descriptor_t) == 16, "ool desc size 16");

typedef u32 mach_msg_trailer_type_t;
#define MACH_MSG_TRAILER_FORMAT_0   0u

typedef struct XIU_PACKED mach_msg_trailer {
    mach_msg_trailer_type_t msgh_trailer_type;
    mach_msg_size_t         msgh_trailer_size;
} mach_msg_trailer_t;

typedef struct XIU_PACKED mach_msg_audit_trailer {
    mach_msg_trailer_type_t msgh_trailer_type;
    mach_msg_size_t         msgh_trailer_size;
    u32                     msgh_seqno;
    xiu_pid_t               msgh_sender_pid;
    xiu_uid_t               msgh_sender_uid;
} mach_msg_audit_trailer_t;

typedef struct ipc_kmsg {
    struct ipc_kmsg        *ikm_next;
    struct ipc_kmsg        *ikm_prev;

    mach_msg_size_t         ikm_size;
    mach_msg_size_t         ikm_header_offset;
    mach_msg_header_t      *ikm_header;

    ipc_port_t             *ikm_remote_port;
    ipc_port_t             *ikm_local_port;
    mach_port_type_t        ikm_remote_right;
    mach_port_type_t        ikm_local_right;

    xiu_pid_t               ikm_sender_pid;
    xiu_uid_t               ikm_sender_uid;
    u64                     ikm_send_time;

    mach_port_seqno_t       ikm_seqno;

    u32                     ikm_ool_count;
    struct {
        xiu_vaddr_t         addr;
        xiu_size_t          size;
    } ikm_ool_regions[16];

    u8 XIU_ALIGNED(64)      ikm_data[];
} ipc_kmsg_t;

ipc_kmsg_t *ipc_kmsg_alloc(mach_msg_size_t msg_size);
void        ipc_kmsg_free(ipc_kmsg_t *kmsg);

XIU_WARN_UNUSED
xiu_error_t ipc_kmsg_copyin(ipc_kmsg_t *kmsg,
                             xiu_vaddr_t user_header,
                             ipc_space_t *space);

XIU_WARN_UNUSED
xiu_error_t ipc_kmsg_copyout(ipc_kmsg_t *kmsg,
                              xiu_vaddr_t user_buf,
                              mach_msg_size_t buf_size,
                              ipc_space_t *space);

XIU_WARN_UNUSED
xiu_error_t ipc_mqueue_send(ipc_port_t *port, ipc_kmsg_t *kmsg,
                             mach_msg_timeout_t timeout_ms);

XIU_WARN_UNUSED
xiu_error_t ipc_mqueue_receive(ipc_port_t *port, ipc_kmsg_t **kmsg_out,
                                mach_msg_timeout_t timeout_ms);

#endif
