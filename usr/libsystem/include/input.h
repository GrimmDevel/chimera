// userspace input event types
#pragma once
#ifndef _XIU_INPUT_H_
#define _XIU_INPUT_H_

typedef enum {
  XIU_EVENT_MOUSE_MOVED = 0,
  XIU_EVENT_MOUSE_DOWN = 1,
  XIU_EVENT_MOUSE_UP = 2,
  XIU_EVENT_MOUSE_CLICKED = 3,
  XIU_EVENT_KEY_PRESSED = 4,
  XIU_EVENT_KEY_RELEASED = 5,
  XIU_EVENT_MOUSE_SCROLLED = 6,
} xiu_event_type_t;

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
} xiu_event_t;

#endif
