/* =============================================================================
 * Chimera Operating System — Service Management (launchd)
 * kernel/include/kernel/launchd.h
 * ============================================================================= */

#pragma once
#ifndef CHIMERA_LAUNCHD_H
#define CHIMERA_LAUNCHD_H

#include <kernel/chimera_types.h>
#include <kernel/ipc_port.h>

#define CHIMERA_LAUNCHD_PORT_NAME "com.xiu.launchd"

// service registration

typedef struct chimera_service {
    char                srv_name[128];  // e.g. com.xiu.WindowServer
    ipc_port_t         *srv_port;       // registered Mach port
    chimera_pid_t           srv_owner;      // pid of the service provider
    
    struct chimera_service *srv_next;
} chimera_service_t;

// launchd control interface

typedef enum {
    LAUNCHD_MSG_REGISTER_SERVICE = 100,
    LAUNCHD_MSG_LOOKUP_SERVICE   = 101,
    LAUNCHD_MSG_CHECKIN          = 102,
} launchd_msg_id_t;

/**
 * launchd_chimera_start — Called by the kernel to spawn PID 1.
 */
chimera_error_t launchd_chimera_start(void);

#endif /* CHIMERA_LAUNCHD_H */
