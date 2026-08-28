/* =============================================================================
 * Chimera Operating System — Darwin Mach Bootstrap Service API
 * usr/libsystem/include/bootstrap.h
 * ============================================================================= */

#pragma once
#ifndef _BOOTSTRAP_H_
#define _BOOTSTRAP_H_

#ifdef __cplusplus
extern "C" {
#endif

#define BOOTSTRAP_SUCCESS           0
#define BOOTSTRAP_NOT_PRIVILEGED    1100
#define BOOTSTRAP_NAME_IN_USE       1101
#define BOOTSTRAP_UNKNOWN_SERVICE   1102
#define BOOTSTRAP_SERVICE_ACTIVE    1103
#define BOOTSTRAP_BAD_COUNT         1104
#define BOOTSTRAP_NO_MEMORY         1105

#define BOOTSTRAP_MAX_NAME_LEN      128

typedef unsigned int mach_port_t;
typedef int kern_return_t;

extern mach_port_t bootstrap_port;

kern_return_t bootstrap_register(mach_port_t bp, const char *service_name, mach_port_t sp);
kern_return_t bootstrap_look_up(mach_port_t bp, const char *service_name, mach_port_t *sp);

#ifdef __cplusplus
}
#endif

#endif /* _BOOTSTRAP_H_ */
