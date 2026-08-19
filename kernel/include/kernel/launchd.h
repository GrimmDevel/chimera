/* =============================================================================
 * XIU Operating System — Service Management (launchd)
 * kernel/include/kernel/launchd.h
 * ============================================================================= */

#pragma once
#ifndef XIU_LAUNCHD_H
#define XIU_LAUNCHD_H

#include <kernel/xiu_types.h>
#include <kernel/ipc_port.h>

#define XIU_LAUNCHD_PORT_NAME "com.xiu.launchd"

// service registration

typedef struct xiu_service {
    char                srv_name[128];  // e.g. com.xiu.WindowServer
    ipc_port_t         *srv_port;       // registered Mach port
    xiu_pid_t           srv_owner;      // pid of the service provider
    
    struct xiu_service *srv_next;
} xiu_service_t;

// launchd control interface

typedef enum {
    LAUNCHD_MSG_REGISTER_SERVICE = 100,
    LAUNCHD_MSG_LOOKUP_SERVICE   = 101,
    LAUNCHD_MSG_CHECKIN          = 102,
} launchd_msg_id_t;

/**
 * launchd_xiu_start — Called by the kernel to spawn PID 1.
 */
xiu_error_t launchd_xiu_start(void);

#endif /* XIU_LAUNCHD_H */
