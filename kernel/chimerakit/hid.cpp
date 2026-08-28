// ps2 + usb hid input layer
#include <kernel/input.h>
#include <kernel/io.h>
#include <kernel/ipc_message.h>
#include <kernel/ipc_port.h>
#include <kernel/panic.h>
#include <kernel/chimera_types.h>

extern "C" void console_in_push(char c);
extern "C" void console_scroll_viewport(int delta);
extern "C" void console_scroll_to_bottom(void);

#define PS2_DATA 0x60
#define PS2_CMD 0x64
#define PS2_STATUS 0x64

namespace XIUKit {

template <typename T, size_t Size> class RingBuffer {
private:
  T data[Size];
  size_t head = 0;
  size_t tail = 0;
  size_t count = 0;

public:
  bool push(const T &item) {
    if (count == Size)
      return false;
    data[tail] = item;
    tail = (tail + 1) % Size;
    count++;
    return true;
  }

  bool pop(T *item) {
    if (count == 0)
      return false;
    *item = data[head];
    head = (head + 1) % Size;
    count--;
    return true;
  }

  size_t size() const { return count; }
};

class HIDDriver {
private:
  u8 mouse_cycle = 0;
  u8 mouse_byte[4];
  u8 mouse_packet_size = 3;
  bool mouse_has_wheel = false;

  bool lshift_down = false;
  bool rshift_down = false;
  bool lctrl_down = false;
  bool rctrl_down = false;
  bool lalt_down = false;
  bool ralt_down = false;
  bool lcmd_down = false;
  bool rcmd_down = false;
  bool capslock_active = false;
  bool is_extended = false;

  RingBuffer<chimera_event_t, 1024> event_queue;
  RingBuffer<chimera_event_t, 1024> mouse_queue;

  void ps2_wait(bool data) {
    u32 timeout = 100000;
    while (timeout--) {
      if (data == 0 && (inb(PS2_STATUS) & 2) == 0)
        return;
      if (data == 1 && (inb(PS2_STATUS) & 1) == 1)
        return;
    }
  }

  void ps2_write(u8 port, u8 val) {
    ps2_wait(0);
    outb(port, val);
  }

  u8 ps2_read() {
    ps2_wait(1);
    return inb(PS2_DATA);
  }

  void ps2_flush() {
    u32 guard = 256;
    while (guard-- && (inb(PS2_STATUS) & 1))
      (void)inb(PS2_DATA);
  }

  u32 get_modifiers() const {
    u32 mods = 0;
    if (lshift_down || rshift_down)
      mods |= CHIMERA_MOD_SHIFT;
    if (lctrl_down || rctrl_down)
      mods |= CHIMERA_MOD_CTRL;
    if (lalt_down || ralt_down)
      mods |= CHIMERA_MOD_ALT;
    if (lcmd_down || rcmd_down)
      mods |= CHIMERA_MOD_CMD;
    if (capslock_active)
      mods |= CHIMERA_MOD_CAPSLOCK;
    if (lshift_down)
      mods |= CHIMERA_MOD_LSHIFT;
    if (rshift_down)
      mods |= CHIMERA_MOD_RSHIFT;
    if (lctrl_down)
      mods |= CHIMERA_MOD_LCTRL;
    if (rctrl_down)
      mods |= CHIMERA_MOD_RCTRL;
    if (lalt_down)
      mods |= CHIMERA_MOD_LALT;
    if (ralt_down)
      mods |= CHIMERA_MOD_RALT;
    if (lcmd_down)
      mods |= CHIMERA_MOD_LCMD;
    if (rcmd_down)
      mods |= CHIMERA_MOD_RCMD;
    return mods;
  }

  u32 decode_scancode(u8 scancode, bool extended, u32 mods) {
    if (extended) {
      switch (scancode) {
      case 0x48:
        return 0x1001;
      case 0x50:
        return 0x1002;
      case 0x4B:
        return 0x1003;
      case 0x4D:
        return 0x1004;
      case 0x47:
        return 0x1005;
      case 0x4F:
        return 0x1006;
      case 0x49:
        return 0x1007;
      case 0x51:
        return 0x1008;
      case 0x53:
        return 0x7F;
      default:
        return 0;
      }
    }

    if (scancode >= 128)
      return 0;

    static const char normal[128] = {
        0,   27,  '1',  '2',  '3',  '4', '5', '6',  '7', '8', '9', '0',
        '-', '=', '\b', '\t', 'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
        'o', 'p', '[',  ']',  '\n', 0,   'a', 's',  'd', 'f', 'g', 'h',
        'j', 'k', 'l',  ';',  '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
        'b', 'n', 'm',  ',',  '.',  '/', 0,   '*',  0,   ' ', 0};
    static const char shifted[128] = {
        0,   27,  '!',  '@',  '#',  '$', '%', '^', '&', '*', '(', ')',
        '_', '+', '\b', '\t', 'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',
        'O', 'P', '{',  '}',  '\n', 0,   'A', 'S', 'D', 'F', 'G', 'H',
        'J', 'K', 'L',  ':',  '"',  '~', 0,   '|', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M',  '<',  '>',  '?', 0,   '*', 0,   ' ', 0};

    bool shift = (mods & CHIMERA_MOD_SHIFT) != 0;
    bool caps = (mods & CHIMERA_MOD_CAPSLOCK) != 0;
    char ch_normal = normal[scancode];
    char ch_shifted = shifted[scancode];

    char ch = 0;
    if (ch_normal >= 'a' && ch_normal <= 'z') {
      ch = (shift ^ caps) ? ch_shifted : ch_normal;
    } else {
      ch = shift ? ch_shifted : ch_normal;
    }

    // ctrl shortcuts
    if (mods & CHIMERA_MOD_CTRL) {
      if (ch >= 'a' && ch <= 'z')
        return (u32)(ch - 'a' + 1);
      if (ch >= 'A' && ch <= 'Z')
        return (u32)(ch - 'A' + 1);
      if (ch == '[' || ch == '{')
        return 27;
      if (ch == '\\' || ch == '|')
        return 28;
      if (ch == ']' || ch == '}')
        return 29;
      if (ch == '^' || ch == '6')
        return 30;
      if (ch == '_' || ch == '-')
        return 31;
      if (ch == ' ' || ch == '@')
        return 0;
    }

    // gui / cmd shortcuts
    if (mods & CHIMERA_MOD_CMD) {
      if (ch == 's' || ch == 'S')
        return 19;
      if (ch == 'q' || ch == 'Q')
        return 17;
      if (ch == 'f' || ch == 'F')
        return 6;
      if (ch == 'c' || ch == 'C')
        return 3;
      if (ch == 'k' || ch == 'K')
        return 12;
      if (ch == 'w' || ch == 'W')
        return 23;
      if (ch == 'a' || ch == 'A')
        return 1;
      if (ch == 'z' || ch == 'Z')
        return 26;
      if (ch == 'x' || ch == 'X')
        return 24;
      if (ch == 'v' || ch == 'V')
        return 22;
      if (ch == '\b')
        return 21;
    }

    if (mods & CHIMERA_MOD_ALT) {
      if (ch == '\b')
        return 23;
    }

    return (u32)ch;
  }

public:
  void init() {
    u8 probe = inb(PS2_STATUS);
    if (probe == 0xFF) {
      kprintf("[ChimeraKit] HID: No PS/2 controller on port 0x64\n");
      return;
    }

    kprintf("[ChimeraKit] HID: Initializing input subsystem (PS/2 & SMM USB "
            "Emulation)\n");

    u32 guard = 256;
    while (guard-- && (inb(PS2_STATUS) & 1))
      (void)inb(PS2_DATA);

    ps2_write(PS2_CMD, 0x20);
    u8 status = ps2_read();
    if (status != 0xFF) {
      status |= 0x03;
      status &= ~0x30;
      ps2_write(PS2_CMD, 0x60);
      ps2_write(PS2_DATA, status);
    }

    ps2_wait(0);
    outb(PS2_CMD, 0xAE);
    ps2_wait(0);
    outb(PS2_CMD, 0xA8);
  }

  void handle_irq() {
    while (true) {
      u8 status = inb(PS2_STATUS);
      if (!(status & 0x01))
        break;

      bool is_mouse_packet = (status & 0x20) != 0;
      u8 b = inb(PS2_DATA);

      if (is_mouse_packet) {
        if (mouse_cycle == 0 && !(b & 0x08))
          continue;

        mouse_byte[mouse_cycle++] = b;
        if (mouse_cycle >= mouse_packet_size) {
          mouse_cycle = 0;
          u8 state = mouse_byte[0];

          if (!(state & 0xC0)) {
            i32 dx = (i32)mouse_byte[1];
            i32 dy = (i32)mouse_byte[2];

            if (state & 0x10)
              dx |= ~0xFF;
            if (state & 0x20)
              dy |= ~0xFF;

            chimera_event_t ev;
            ev.type = CHIMERA_EVENT_MOUSE_MOVED;
            ev.data.mouse.delta_x = dx;
            ev.data.mouse.delta_y = -dy; // ps2 hardware y is inverted
            ev.data.mouse.delta_z = 0;
            ev.data.mouse.buttons = state & 0x7;
            mouse_queue.push(ev);
          }
        }
      } else {
        if (b == 0xE0) {
          is_extended = true;
          continue;
        }

        bool release = (b & 0x80) != 0;
        u8 scancode = b & 0x7F;
        bool extended = is_extended;
        is_extended = false;

        if (!extended) {
          if (scancode == 0x2A) {
            lshift_down = !release;
            goto emit_event;
          }
          if (scancode == 0x36) {
            rshift_down = !release;
            goto emit_event;
          }
          if (scancode == 0x1D) {
            lctrl_down = !release;
            goto emit_event;
          }
          if (scancode == 0x38) {
            lalt_down = !release;
            goto emit_event;
          }
          if (scancode == 0x3A && !release) {
            capslock_active = !capslock_active;
            goto emit_event;
          }
        } else {
          if (scancode == 0x1D) {
            rctrl_down = !release;
            goto emit_event;
          }
          if (scancode == 0x38) {
            ralt_down = !release;
            goto emit_event;
          }
          if (scancode == 0x5B) {
            lcmd_down = !release;
            goto emit_event;
          }
          if (scancode == 0x5C) {
            rcmd_down = !release;
            goto emit_event;
          }
        }

      emit_event:
        u32 mods = get_modifiers();
        u32 unicode = decode_scancode(scancode, extended, mods);

        chimera_event_t ev;
        ev.type = release ? CHIMERA_EVENT_KEY_RELEASED : CHIMERA_EVENT_KEY_PRESSED;
        ev.data.keyboard.keycode = scancode | (extended ? 0x8000 : 0);
        ev.data.keyboard.unicode = release ? 0 : unicode;
        ev.data.keyboard.modifiers = mods;
        event_queue.push(ev);

        if (!release && unicode != 0) {
          if (unicode == 0x1007 ||
              ((mods & CHIMERA_MOD_SHIFT) && unicode == 0x1001)) {
            console_scroll_viewport(unicode == 0x1007 ? 10 : 1);
          } else if (unicode == 0x1008 ||
                     ((mods & CHIMERA_MOD_SHIFT) && unicode == 0x1002)) {
            console_scroll_viewport(unicode == 0x1008 ? -10 : -1);
          } else if ((mods & CHIMERA_MOD_SHIFT) && unicode == 0x1005) {
            console_scroll_viewport(10000);
          } else if ((mods & CHIMERA_MOD_SHIFT) && unicode == 0x1006) {
            console_scroll_to_bottom();
          } else {
            console_scroll_to_bottom();

            if (unicode == 0x1001) {
              console_in_push('\033');
              console_in_push('[');
              console_in_push('A');
            } else if (unicode == 0x1002) {
              console_in_push('\033');
              console_in_push('[');
              console_in_push('B');
            } else if (unicode == 0x1003) {
              console_in_push('\033');
              console_in_push('[');
              console_in_push('D');
            } else if (unicode == 0x1004) {
              console_in_push('\033');
              console_in_push('[');
              console_in_push('C');
            } else if (unicode == 0x1005) {
              console_in_push('\033');
              console_in_push('[');
              console_in_push('H');
            } else if (unicode == 0x1006) {
              console_in_push('\033');
              console_in_push('[');
              console_in_push('F');
            } else {
              console_in_push((char)unicode);
            }
          }
        }
      }
    }
  }

  void push_key_event(u32 scancode, u32 unicode, u32 mods, bool release) {
    chimera_event_t ev;
    ev.type = release ? CHIMERA_EVENT_KEY_RELEASED : CHIMERA_EVENT_KEY_PRESSED;
    ev.data.keyboard.keycode = scancode;
    ev.data.keyboard.unicode = release ? 0 : unicode;
    ev.data.keyboard.modifiers = mods;
    event_queue.push(ev);

    if (!release && unicode != 0) {
      if (unicode == 0x1001) {
        console_in_push('\033');
        console_in_push('[');
        console_in_push('A');
      } else if (unicode == 0x1002) {
        console_in_push('\033');
        console_in_push('[');
        console_in_push('B');
      } else if (unicode == 0x1003) {
        console_in_push('\033');
        console_in_push('[');
        console_in_push('D');
      } else if (unicode == 0x1004) {
        console_in_push('\033');
        console_in_push('[');
        console_in_push('C');
      } else if (unicode == 0x1005) {
        console_in_push('\033');
        console_in_push('[');
        console_in_push('H');
      } else if (unicode == 0x1006) {
        console_in_push('\033');
        console_in_push('[');
        console_in_push('F');
      } else {
        console_in_push((char)unicode);
      }
    }
  }

  void push_mouse_event(i32 dx, i32 dy, i32 dz, u32 buttons) {
    chimera_event_t ev;
    ev.type = CHIMERA_EVENT_MOUSE_MOVED;
    ev.data.mouse.delta_x = dx;
    ev.data.mouse.delta_y = dy;
    ev.data.mouse.delta_z = dz;
    ev.data.mouse.buttons = buttons;
    mouse_queue.push(ev);
  }

  size_t read_events(chimera_event_t *buf, size_t max_count) {
    size_t read_count = 0;
    while (read_count < max_count && event_queue.pop(&buf[read_count])) {
      read_count++;
    }
    return read_count;
  }

  size_t read_mouse_events(chimera_event_t *buf, size_t max_count) {
    handle_irq();
    size_t read_count = 0;
    while (read_count < max_count && mouse_queue.pop(&buf[read_count])) {
      read_count++;
    }
    return read_count;
  }

  bool has_events() {
    handle_irq();
    return event_queue.size() > 0;
  }

  bool has_mouse_events() {
    handle_irq();
    return mouse_queue.size() > 0;
  }
};

static HIDDriver s_hid;

} // namespace XIUKit

extern "C" void chimerakit_xhci_poll(void);

extern "C" void chimerakit_hid_irq_handler(void) {
  XIUKit::s_hid.handle_irq();
  chimerakit_xhci_poll();
}

extern "C" void chimerakit_hid_poll(void) {
  XIUKit::s_hid.handle_irq();
  chimerakit_xhci_poll();
}

extern "C" void chimerakit_hid_init() { XIUKit::s_hid.init(); }

extern "C" void chimerakit_hid_push_key_event(u32 scancode, u32 unicode, u32 mods,
                                          bool release) {
  XIUKit::s_hid.push_key_event(scancode, unicode, mods, release);
}

extern "C" void chimerakit_hid_push_mouse_event(i32 dx, i32 dy, i32 dz,
                                            u32 buttons) {
  XIUKit::s_hid.push_mouse_event(dx, dy, dz, buttons);
}

extern "C" size_t chimerakit_hid_read_mouse(chimera_event_t *buf, size_t count) {
  chimerakit_xhci_poll();
  return XIUKit::s_hid.read_mouse_events(buf, count);
}

extern "C" size_t chimerakit_hid_read_kbd(chimera_event_t *buf, size_t count) {
  chimerakit_xhci_poll();
  return XIUKit::s_hid.read_events(buf, count);
}

extern "C" bool chimerakit_hid_has_mouse(void) {
  chimerakit_xhci_poll();
  return XIUKit::s_hid.has_mouse_events();
}

extern "C" bool chimerakit_hid_has_kbd(void) {
  chimerakit_xhci_poll();
  return XIUKit::s_hid.has_events();
}
