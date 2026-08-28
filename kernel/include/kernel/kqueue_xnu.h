/* =============================================================================
 * Chimera Operating System — Apple XNU kqueue / kevent Subsystem
 * kernel/include/kernel/kqueue_xnu.h
 * Derived from XNU bsd/sys/event.h
 * ============================================================================= */

#ifndef CHIMERA_KQUEUE_XNU_H
#define CHIMERA_KQUEUE_XNU_H

#include <kernel/chimera_types.h>

#ifdef __cplusplus
extern "C" {
#endif

// filters
#define EVFILT_READ     (-1)
#define EVFILT_WRITE    (-2)
#define EVFILT_AIO      (-3)
#define EVFILT_VNODE    (-4)
#define EVFILT_PROC     (-5)
#define EVFILT_SIGNAL   (-6)
#define EVFILT_TIMER    (-7)

// flags
#define EV_ADD          0x0001      /* Add event to kqueue                     */
#define EV_DELETE       0x0002      /* Delete event from kqueue                */
#define EV_ENABLE       0x0004      /* Enable event                            */
#define EV_DISABLE      0x0008      /* Disable event                           */
#define EV_ONESHOT      0x0010      /* Trigger event only once                 */
#define EV_CLEAR        0x0020      /* Reset state after status retrieval      */
#define EV_ERROR        0x4000      /* Filter error                            */

struct kevent {
    uintptr_t ident; /* identifier for this event (fd */ , pid, handle)
    int16_t   filter;     // filter for event
    uint16_t  flags;      // action flags
    uint32_t  fflags;     // filter-specific flags
    intptr_t  data;       // filter-specific data
    void     *udata;      // opaque user data identifier
};

#ifdef __cplusplus
}
#endif

#endif /* CHIMERA_KQUEUE_XNU_H */
