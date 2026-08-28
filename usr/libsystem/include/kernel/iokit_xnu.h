// iokit driver and event headers
#ifndef CHIMERA_IOKIT_XNU_H
#define CHIMERA_IOKIT_XNU_H

#include <kernel/chimera_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef u32 kern_return_t;
typedef u32 IOReturn;

#define kIOReturnSuccess            0x00000000
#define kIOReturnError              0xe00002bc
#define kIOReturnNoMemory           0xe00002bd
#define kIOReturnNoResources        0xe00002be
#define kIOReturnIPCError           0xe00002bf
#define kIOReturnNoDevice           0xe00002c0
#define kIOReturnNotPrivileged      0xe00002c1
#define kIOReturnBadArgument        0xe00002c2
#define kIOReturnLockedRead         0xe00002c3
#define kIOReturnLockedWrite        0xe00002c4
#define kIOReturnExclusiveAccess    0xe00002c5
#define kIOReturnBadMessageID       0xe00002c6
#define kIOReturnUnsupported        0xe00002c7
#define kIOReturnVMError            0xe00002c8
#define kIOReturnInternalError      0xe00002c9

typedef enum {
    kIOHIDEventTypeNull             = 0,
    kIOHIDEventTypeKeyboard         = 3,
    kIOHIDEventTypeMouse            = 1,
    kIOHIDEventTypeScroll           = 2,
    kIOHIDEventTypePointer          = 11,
    kIOHIDEventTypeGesture          = 17
} IOHIDEventType;

typedef struct {
    i32 x;
    i32 y;
    i32 delta_x;
    i32 delta_y;
    u32 button_mask;
    u64 time_stamp;
} IOHIDMouseEvent;

typedef struct {
    u16 usage_page;
    u16 usage;
    bool key_down;
    u32 time_stamp;
} IOHIDKeyboardEvent;

#ifdef __cplusplus
}
#endif

#endif
