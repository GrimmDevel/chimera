// input event definitions
#ifndef CHIMERA_INPUT_H
#define CHIMERA_INPUT_H

#include <kernel/chimera_types.h>

#define CHIMERA_MOD_SHIFT       (1 << 0)
#define CHIMERA_MOD_CTRL        (1 << 1)
#define CHIMERA_MOD_ALT         (1 << 2)
#define CHIMERA_MOD_CMD         (1 << 3)
#define CHIMERA_MOD_CAPSLOCK    (1 << 4)
#define CHIMERA_MOD_NUMLOCK     (1 << 5)
#define CHIMERA_MOD_LSHIFT      (1 << 6)
#define CHIMERA_MOD_RSHIFT      (1 << 7)
#define CHIMERA_MOD_LCTRL       (1 << 8)
#define CHIMERA_MOD_RCTRL       (1 << 9)
#define CHIMERA_MOD_LALT        (1 << 10)
#define CHIMERA_MOD_RALT        (1 << 11)
#define CHIMERA_MOD_LCMD        (1 << 12)
#define CHIMERA_MOD_RCMD        (1 << 13)

typedef enum {
  CHIMERA_EVENT_MOUSE_MOVED = 0,
  CHIMERA_EVENT_MOUSE_DOWN = 1,
  CHIMERA_EVENT_MOUSE_UP = 2,
  CHIMERA_EVENT_MOUSE_CLICKED = 3,
  CHIMERA_EVENT_KEY_PRESSED = 4,
  CHIMERA_EVENT_KEY_RELEASED = 5,
  CHIMERA_EVENT_MOUSE_SCROLLED = 6,
} chimera_event_type_t;

typedef struct chimera_event {
  chimera_event_type_t type;
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
} chimera_event_t;

#endif
