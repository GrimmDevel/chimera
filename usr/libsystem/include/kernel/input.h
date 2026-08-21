// input event definitions
#ifndef XIU_INPUT_H
#define XIU_INPUT_H

#include <kernel/xiu_types.h>

#define XIU_MOD_SHIFT       (1 << 0)
#define XIU_MOD_CTRL        (1 << 1)
#define XIU_MOD_ALT         (1 << 2)
#define XIU_MOD_CMD         (1 << 3)
#define XIU_MOD_CAPSLOCK    (1 << 4)
#define XIU_MOD_NUMLOCK     (1 << 5)
#define XIU_MOD_LSHIFT      (1 << 6)
#define XIU_MOD_RSHIFT      (1 << 7)
#define XIU_MOD_LCTRL       (1 << 8)
#define XIU_MOD_RCTRL       (1 << 9)
#define XIU_MOD_LALT        (1 << 10)
#define XIU_MOD_RALT        (1 << 11)
#define XIU_MOD_LCMD        (1 << 12)
#define XIU_MOD_RCMD        (1 << 13)

typedef enum {
  XIU_EVENT_MOUSE_MOVED = 0,
  XIU_EVENT_MOUSE_DOWN = 1,
  XIU_EVENT_MOUSE_UP = 2,
  XIU_EVENT_MOUSE_CLICKED = 3,
  XIU_EVENT_KEY_PRESSED = 4,
  XIU_EVENT_KEY_RELEASED = 5,
  XIU_EVENT_MOUSE_SCROLLED = 6,
} xiu_event_type_t;

typedef struct xiu_event {
  xiu_event_type_t type;
  union {
    struct {
      i32 delta_x;
      i32 delta_y;
      i32 delta_z;
      u32 buttons;
    } mouse;
    struct {
      u32 keycode;
      u32 unicode;
      u32 modifiers;
    } keyboard;
  } data;
} xiu_event_t;

#endif
