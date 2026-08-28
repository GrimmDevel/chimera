// xnu mach definitions
#ifndef CHIMERA_MACH_XNU_H
#define CHIMERA_MACH_XNU_H

#include <kernel/chimera_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef u32 mach_port_t;
typedef u32 mach_port_name_t;
typedef u32 mach_port_right_t;
typedef u32 mach_msg_bits_t;
typedef u32 mach_msg_size_t;
typedef u32 mach_msg_id_t;
typedef i32 mach_msg_return_t;

#define MACH_PORT_RIGHT_SEND        ((mach_port_right_t) 0)
#define MACH_PORT_RIGHT_RECEIVE     ((mach_port_right_t) 1)
#define MACH_PORT_RIGHT_SEND_ONCE   ((mach_port_right_t) 2)
#define MACH_PORT_RIGHT_PORT_SET    ((mach_port_right_t) 3)
#define MACH_PORT_RIGHT_DEAD_NAME   ((mach_port_right_t) 4)

#define MACH_PORT_NULL              ((mach_port_t) 0)
#define MACH_PORT_DEAD              ((mach_port_t) ~0U)

typedef struct {
    mach_msg_bits_t       msgh_bits;
    mach_msg_size_t       msgh_size;
    mach_port_t           msgh_remote_port;
    mach_port_t           msgh_local_port;
    mach_port_name_t      msgh_voucher_port;
    mach_msg_id_t         msgh_id;
} mach_msg_header_t;

#define MACH_SEND_MSG             0x00000001
#define MACH_RCV_MSG              0x00000002
#define MACH_RCV_LARGE            0x00000004
#define MACH_SEND_TIMEOUT         0x00000010
#define MACH_RCV_TIMEOUT          0x00000100

#define MACH_MSG_SUCCESS          0x00000000
#define MACH_SEND_INVALID_DATA    0x10000002
#define MACH_SEND_INVALID_DEST    0x10000003
#define MACH_SEND_TIMED_OUT       0x10000004
#define MACH_RCV_TIMED_OUT        0x10000205

#ifdef __cplusplus
}
#endif

#endif
