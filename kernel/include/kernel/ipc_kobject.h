// mach kernel objects and mig rpc subsystem
#pragma once
#ifndef CHIMERA_IPC_KOBJECT_H
#define CHIMERA_IPC_KOBJECT_H

#include <kernel/chimera_types.h>
#include <kernel/ipc_port.h>
#include <kernel/ipc_message.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IKOT_NONE           = 0,
    IKOT_TASK           = 1,
    IKOT_HOST           = 2,
    IKOT_CLOCK          = 3,
    IKOT_BOOTSTRAP      = 4,
} ipc_kobject_type_t;

#define MACH_VM_ALLOCATE_ID         4800
#define MACH_VM_DEALLOCATE_ID       4801
#define MACH_VM_PROTECT_ID          4802
#define MACH_VM_READ_ID             4805
#define MACH_VM_WRITE_ID            4806

#define TASK_GET_SPECIAL_PORT_ID    3403
#define TASK_SET_SPECIAL_PORT_ID    3404
#define TASK_INFO_ID                3405
#define HOST_INFO_ID                200

#define TASK_KERNEL_PORT            1
#define TASK_HOST_PORT              2
#define TASK_BOOTSTRAP_PORT         4

void ipc_kobject_set(struct ipc_port *port, void *kobject, ipc_kobject_type_t type);
chimera_error_t ipc_kobject_server(struct ipc_port *port, ipc_kmsg_t *request_kmsg);

#ifdef __cplusplus
}
#endif

#endif
