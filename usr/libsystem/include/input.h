// userspace input event types
#pragma once
#ifndef _XIU_INPUT_H_
#define _XIU_INPUT_H_

typedef enum {
  CHIMERA_EVENT_MOUSE_MOVED = 0,
  CHIMERA_EVENT_MOUSE_DOWN = 1,
  CHIMERA_EVENT_MOUSE_UP = 2,
  CHIMERA_EVENT_MOUSE_CLICKED = 3,
  CHIMERA_EVENT_KEY_PRESSED = 4,
  CHIMERA_EVENT_KEY_RELEASED = 5,
  CHIMERA_EVENT_MOUSE_SCROLLED = 6,
} chimera_event_type_t;

typedef struct __attribute__((packed)) {
  unsigned int type;
  union {
    struct {
      int          delta_x;
      int          delta_y;
      int          delta_z;
      unsigned int buttons;
    } mouse;
    struct {
      unsigned int keycode;
      unsigned int unicode;
      unsigned int modifiers;
    } keyboard;
  } data;
} chimera_event_t;

#endif
