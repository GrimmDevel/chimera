/*
 * XIU Operating System — Console Core & Darwin Multiplexer
 * Adapts Darwin/XNU video_console + serial for Ring 0/Ring 3.
 */

#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/spinlock.h>
#include <kernel/video_console.h>
#include <kernel/xiu_types.h>
#include <stdarg.h>

#define COM1_PORT 0x3f8

static inline void outb(u16 port, u8 val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline u8 inb(u16 port) {
  u8 ret;
  __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static bool s_serial_present = false;

void serial_init(void) {
  outb(COM1_PORT + 1, 0x00);
  outb(COM1_PORT + 3, 0x80);
  outb(COM1_PORT + 0, 0x03);
  outb(COM1_PORT + 1, 0x00);
  outb(COM1_PORT + 3, 0x03);
  outb(COM1_PORT + 2, 0xC7);
  outb(COM1_PORT + 4, 0x0B);
  outb(COM1_PORT + 7, 0xAE);
  if (inb(COM1_PORT + 7) == 0xAE) {
    s_serial_present = true;
  }
}

void serial_putc(char c) {
  if (!s_serial_present)
    return;
  int timeout = 10000;
  while (!(inb(COM1_PORT + 5) & 0x20) && --timeout > 0)
    ;
  if (timeout > 0) {
    outb(COM1_PORT, c);
  }
}

void serial_puts(const char *s) {
  if (!s)
    return;
  while (*s) {
    if (*s == '\n')
      serial_putc('\r');
    serial_putc(*s++);
  }
}

bool serial_has_data(void) {
  if (!s_serial_present)
    return false;
  return (inb(COM1_PORT + 5) & 0x01) != 0;
}

char serial_getc(void) {
  if (!s_serial_present)
    return 0;
  while (!serial_has_data())
    ;
  return inb(COM1_PORT);
}

void console_init(void) { serial_init(); }

void console_init_display(unsigned long baseaddr, uint64_t physaddr,
                          unsigned int width, unsigned int height,
                          unsigned int depth, unsigned int pitch) {
  video_console_init(baseaddr, physaddr, width, height, depth, pitch);
}

void console_putc(char c) {
  if (c == '\n') {
    serial_putc('\r');
    serial_putc('\n');
  } else {
    serial_putc(c);
  }
  vc_putchar(c);
}

void console_puts(const char *s) {
  if (!s)
    return;
  serial_puts(s);
  vc_puts(s);
}

void serial_write(const char *s, usize len) {
  if (!s_serial_present || !s || len == 0)
    return;
  for (usize i = 0; i < len; i++) {
    char c = s[i];
    if (c == '\n') {
      outb(COM1_PORT, '\r');
    }
    outb(COM1_PORT, c);
  }
}

void console_write(const char *s, usize len) {
  if (!s || len == 0)
    return;
  serial_write(s, len);
  vc_write(s, len);
}

/* -----------------------------------------------------------------------------
 * Keyboard & Serial Line Input Buffer
 * -----------------------------------------------------------------------------
 */
#define IN_BUF_SIZE 256
static char s_in_buf[IN_BUF_SIZE];
static usize s_in_head = 0;
static usize s_in_tail = 0;
static usize s_in_count = 0;
static bool s_line_ready = false;
static spinlock_t s_in_lock = SPINLOCK_INIT;

#define RAW_BUF_SIZE 512
static char s_raw_buf[RAW_BUF_SIZE];
static usize s_raw_head = 0;
static usize s_raw_tail = 0;
static usize s_raw_count = 0;

static void console_in_char_locked(char c) {
  if (vc_get_raw_mode()) {
    if (s_raw_count < RAW_BUF_SIZE) {
      s_raw_buf[s_raw_head] = c;
      s_raw_head = (s_raw_head + 1) % RAW_BUF_SIZE;
      s_raw_count++;
    }
  } else {
    if (c == '\r')
      c = '\n';
    if (c == '\b' || c == 0x7f) {
      if (s_in_count > 0) {
        s_in_head = (s_in_head + IN_BUF_SIZE - 1) % IN_BUF_SIZE;
        s_in_count--;
        console_putc('\b');
        console_putc(' ');
        console_putc('\b');
      }
    } else if (c == '\n') {
      if (s_in_count < IN_BUF_SIZE - 1) {
        s_in_buf[s_in_head] = '\n';
        s_in_head = (s_in_head + 1) % IN_BUF_SIZE;
        s_in_count++;
        s_line_ready = true;
        console_putc('\n');
      }
    } else if (c == 27) {
      // Escape sequence start: store in buffer without visual echo
      if (s_in_count < IN_BUF_SIZE - 1) {
        s_in_buf[s_in_head] = 27;
        s_in_head = (s_in_head + 1) % IN_BUF_SIZE;
        s_in_count++;
      }
    } else if ((u8)c >= 32 || c == '\t') {
      if (s_in_count < IN_BUF_SIZE - 1) {
        s_in_buf[s_in_head] = c;
        s_in_head = (s_in_head + 1) % IN_BUF_SIZE;
        s_in_count++;

        // If part of an escape sequence (\033[A, \033[B, etc.), do not echo to screen
        bool in_esc = false;
        if (s_in_count >= 2 && s_in_buf[(s_in_head + IN_BUF_SIZE - 2) % IN_BUF_SIZE] == 27) {
          in_esc = true;
        } else if (s_in_count >= 3 && s_in_buf[(s_in_head + IN_BUF_SIZE - 3) % IN_BUF_SIZE] == 27) {
          in_esc = true;
        }

        if (!in_esc) {
          console_putc(c);
        }
      }
    }
  }
}

static void console_poll_serial(void) {
  if (!s_serial_present)
    return;
  while (serial_has_data()) {
    char c = serial_getc();
    irq_flags_t irq = spinlock_lock_irqsave(&s_in_lock);
    console_in_char_locked(c);
    spinlock_unlock_irqrestore(&s_in_lock, irq);
  }
}

extern void xiukit_hid_poll(void);

void console_push_char(char c) {
  irq_flags_t irq = spinlock_lock_irqsave(&s_in_lock);
  console_in_char_locked(c);
  spinlock_unlock_irqrestore(&s_in_lock, irq);
}

void console_in_push(char c) { console_push_char(c); }

void console_scroll_viewport(int delta) {
  if (delta < 0)
    vc_scroll_up(-delta);
  else if (delta > 0)
    vc_scroll_down(delta);
}

void console_scroll_to_bottom(void) {}

void console_fb_set_active(bool active) {
  extern void vc_set_enabled(bool enabled);
  vc_set_enabled(active);
}

usize console_read(char *buf, usize count) {
  if (!buf || count == 0)
    return 0;

  extern void scheduler_yield(void);

  for (;;) {
    console_poll_serial();
    xiukit_hid_poll();

    irq_flags_t irq = spinlock_lock_irqsave(&s_in_lock);
    if (vc_get_raw_mode()) {
      if (s_raw_count > 0) {
        usize n = 0;
        while (n < count && s_raw_count > 0) {
          buf[n++] = s_raw_buf[s_raw_tail];
          s_raw_tail = (s_raw_tail + 1) % RAW_BUF_SIZE;
          s_raw_count--;
        }
        spinlock_unlock_irqrestore(&s_in_lock, irq);
        return n;
      }
    } else {
      if (s_line_ready && s_in_count > 0) {
        usize n = 0;
        while (n < count && s_in_count > 0) {
          char c = s_in_buf[s_in_tail];
          buf[n++] = c;
          s_in_tail = (s_in_tail + 1) % IN_BUF_SIZE;
          s_in_count--;
          if (c == '\n')
            break;
        }
        bool has_another_newline = false;
        for (usize i = 0; i < s_in_count; i++) {
          if (s_in_buf[(s_in_tail + i) % IN_BUF_SIZE] == '\n') {
            has_another_newline = true;
            break;
          }
        }
        s_line_ready = has_another_newline;
        if (s_in_count == 0) {
          s_in_head = 0;
          s_in_tail = 0;
        }
        spinlock_unlock_irqrestore(&s_in_lock, irq);
        return n;
      }
    }
    spinlock_unlock_irqrestore(&s_in_lock, irq);
    scheduler_yield();
  }
}

bool console_has_input(void) {
  console_poll_serial();
  xiukit_hid_poll();
  irq_flags_t irq = spinlock_lock_irqsave(&s_in_lock);
  bool ready = vc_get_raw_mode() ? (s_raw_count > 0) : s_line_ready;
  spinlock_unlock_irqrestore(&s_in_lock, irq);
  return ready;
}

void console_set_raw_mode(bool raw) {
  irq_flags_t irq = spinlock_lock_irqsave(&s_in_lock);
  if (raw && s_in_count > 0) {
    while (s_in_count > 0 && s_raw_count < RAW_BUF_SIZE) {
      s_raw_buf[s_raw_head] = s_in_buf[s_in_tail];
      s_raw_head = (s_raw_head + 1) % RAW_BUF_SIZE;
      s_raw_count++;
      s_in_tail = (s_in_tail + 1) % IN_BUF_SIZE;
      s_in_count--;
    }
    s_line_ready = false;
  }
  vc_set_raw_mode(raw);
  spinlock_unlock_irqrestore(&s_in_lock, irq);
}

typedef struct xiu_darwin_termios {
  u64 c_iflag;
  u64 c_oflag;
  u64 c_cflag;
  u64 c_lflag;
  u8 c_cc[20];
  u8 _pad[4];
  u64 c_ispeed;
  u64 c_ospeed;
} xiu_darwin_termios_t;

typedef struct xiu_winsize_raw {
  u16 ws_row;
  u16 ws_col;
  u16 ws_xpixel;
  u16 ws_ypixel;
} xiu_winsize_raw_t;

xiu_error_t console_ioctl(u64 cmd, xiu_vaddr_t arg) {
  extern xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);
  extern xiu_error_t copyin(const void *uaddr, void *kaddr, usize len);

  // FIODTYPE (0x4004667a or 0x2004667a or 0x8004667a)
  if (cmd == 0x4004667a || cmd == 0x2004667a || cmd == 0x8004667a) {
    if (arg) {
      int d_type = 3; // D_TTY
      return copyout(&d_type, (void *)arg, sizeof(int));
    }
    return XIU_SUCCESS;
  }

  // TIOCGETA (0x40487413 in Darwin 64-bit, 0x402c7413 in FreeBSD, 0x5401 in
  // Linux)
  if (cmd == 0x40487413 || cmd == 0x402c7413 || cmd == 0x5401 ||
      (cmd & 0xff) == 19) {
    xiu_darwin_termios_t dt;
    __builtin_memset(&dt, 0, sizeof(dt));
    dt.c_iflag = 0x00000000;
    dt.c_oflag = 0x00000003; // OPOST | ONLCR
    dt.c_cflag = 0x00004b00; // CREAD | CS8
    dt.c_lflag =
        vc_get_raw_mode()
            ? 0
            : (0x00000100 | 0x00000008 | 0x00000080); // ICANON | ECHO | ISIG
    dt.c_cc[0] = 4;                                   // VEOF (Ctrl-D)
    dt.c_cc[1] = 10;                                  // VEOL (\n)
    dt.c_cc[3] = 127;                                 // VERASE (Backspace)
    dt.c_cc[8] = 3;                                   // VINTR (Ctrl-C)
    dt.c_cc[9] = 28;                                  // VQUIT (Ctrl-\)
    dt.c_cc[10] = 26;                                 // VSUSP (Ctrl-Z)
    dt.c_cc[16] = 1;                                  // VMIN
    dt.c_cc[17] = 0;                                  // VTIME
    dt.c_ispeed = 38400;
    dt.c_ospeed = 38400;
    return copyout(&dt, (void *)arg, sizeof(dt));
  }
  // TIOCSETA, TIOCSETAW, TIOCSETAF (0x80487414..0x80487416 in Darwin 64-bit)
  if (cmd == 0x80487414 || cmd == 0x80487415 || cmd == 0x80487416 ||
      cmd == 0x802c7414 || cmd == 0x802c7415 || cmd == 0x802c7416 ||
      cmd == 0x5402 || cmd == 0x5403 || cmd == 0x5404 || (cmd & 0xff) == 20 ||
      (cmd & 0xff) == 21 || (cmd & 0xff) == 22) {
    xiu_darwin_termios_t dt;
    if (copyin((const void *)arg, &dt, sizeof(dt)) != XIU_SUCCESS)
      return XIU_ERR_INVALID;
    bool raw = false;
    if (cmd == 0x5402 || cmd == 0x5403 || cmd == 0x5404) {
      raw = (dt.c_lflag & 0x0002) == 0; // Linux ICANON
    } else {
      raw = (dt.c_lflag & 0x0100) == 0; // Darwin BSD ICANON
    }
    console_set_raw_mode(raw);
    return XIU_SUCCESS;
  }

  // TIOCGWINSZ
  if (cmd == 0x40087468 || cmd == 0x5413) {
    struct vc_info *vi = vc_get_info();
    xiu_winsize_raw_t ws;
    ws.ws_row = vi ? vi->v_rows : 25;
    ws.ws_col = vi ? vi->v_columns : 80;
    ws.ws_xpixel = vi ? vi->v_width : 1280;
    ws.ws_ypixel = vi ? vi->v_height : 800;
    return copyout(&ws, (void *)arg, sizeof(ws));
  }
  // TTY control / job-control ioctl handlers (TIOCSPGRP, TIOCGPGRP, TIOCSCTTY,
  // etc.)
  static int s_console_pgrp = 0;

  if (cmd == 0x80047476 || cmd == 0x5410 || (cmd & 0xff) == 118) { // TIOCSPGRP
    if (arg) {
      int pgrp = 0;
      if (copyin((const void *)arg, &pgrp, sizeof(int)) == XIU_SUCCESS) {
        s_console_pgrp = pgrp;
      }
    }
    return XIU_SUCCESS;
  }

  if (cmd == 0x40047477 || cmd == 0x540f || (cmd & 0xff) == 119) { // TIOCGPGRP
    if (arg) {
      xiu_task_t *cur = current_task();
      int pgrp = s_console_pgrp;
      if (pgrp <= 0) {
        pgrp = (cur && cur->ta_proc) ? (int)cur->ta_proc->p_pgrp : 1;
      }
      return copyout(&pgrp, (void *)arg, sizeof(int));
    }
    return XIU_SUCCESS;
  }

  if (cmd == 0x20007461 || cmd == 0x20007471 || cmd == 0x2000745e ||
      cmd == 0x80047410 || cmd == 0x80017472 || (cmd & 0xff00) == 0x7400) {
    return XIU_SUCCESS;
  }
  return XIU_SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Kernel Printf & Formatting
 * -----------------------------------------------------------------------------
 */
#define KVPRINTF_BUF_SIZE 1024

static inline void kv_emit_char(char *buf, usize *len, char c) {
  buf[(*len)++] = c;
  if (*len >= KVPRINTF_BUF_SIZE) {
    console_write(buf, *len);
    *len = 0;
  }
}

static inline void kv_emit_str(char *buf, usize *len, const char *s) {
  if (!s)
    s = "(null)";
  while (*s) {
    kv_emit_char(buf, len, *s++);
  }
}

void kvprintf(const char *fmt, va_list args) {
  char out_buf[KVPRINTF_BUF_SIZE];
  usize out_len = 0;

  for (const char *p = fmt; *p != '\0'; p++) {
    if (*p != '%') {
      kv_emit_char(out_buf, &out_len, *p);
      continue;
    }
    p++;

    bool pad_zero = false;
    int width = 0;
    if (*p == '0') {
      pad_zero = true;
      p++;
    }
    while (*p >= '0' && *p <= '9') {
      width = width * 10 + (*p - '0');
      p++;
    }

    bool is_long = false;
    bool is_long_long = false;
    bool is_size_t = false;
    if (*p == 'l') {
      is_long = true;
      p++;
      if (*p == 'l') {
        is_long_long = true;
        p++;
      }
    } else if (*p == 'z') {
      is_size_t = true;
      p++;
    }

    switch (*p) {
    case 'c': {
      char ch = (char)va_arg(args, int);
      kv_emit_char(out_buf, &out_len, ch);
      break;
    }
    case 's': {
      const char *s = va_arg(args, const char *);
      kv_emit_str(out_buf, &out_len, s);
      break;
    }
    case 'u':
    case 'd': {
      u64 u = 0;
      if (is_long_long)
        u = va_arg(args, u64);
      else if (is_long || is_size_t)
        u = va_arg(args, unsigned long);
      else
        u = va_arg(args, u32);

      char num_buf[32];
      int i = 31;
      num_buf[i--] = '\0';
      if (u == 0)
        num_buf[i--] = '0';
      else {
        while (u > 0) {
          num_buf[i--] = (u % 10) + '0';
          u /= 10;
        }
      }
      while (i >= 0 && (30 - i < width))
        num_buf[i--] = pad_zero ? '0' : ' ';
      for (int k = i + 1; num_buf[k]; k++)
        kv_emit_char(out_buf, &out_len, num_buf[k]);
      break;
    }
    case 'x':
    case 'p': {
      u64 val = 0;
      if (*p == 'p') {
        val = (uptr)va_arg(args, void *);
        width = 16;
        pad_zero = true;
        kv_emit_char(out_buf, &out_len, '0');
        kv_emit_char(out_buf, &out_len, 'x');
      } else {
        if (is_long_long)
          val = va_arg(args, u64);
        else if (is_long || is_size_t)
          val = va_arg(args, unsigned long);
        else
          val = va_arg(args, u32);
      }

      if (width == 0) {
        char num_buf[32];
        int i = 31;
        num_buf[i--] = '\0';
        if (val == 0)
          num_buf[i--] = '0';
        else {
          while (val > 0) {
            int nibble = val & 0xf;
            num_buf[i--] = nibble < 10 ? nibble + '0' : nibble - 10 + 'a';
            val >>= 4;
          }
        }
        for (int k = i + 1; num_buf[k]; k++)
          kv_emit_char(out_buf, &out_len, num_buf[k]);
      } else {
        for (int k = (width - 1) * 4; k >= 0; k -= 4) {
          int nibble = (val >> k) & 0xf;
          kv_emit_char(out_buf, &out_len, nibble < 10 ? nibble + '0' : nibble - 10 + 'a');
        }
      }
      break;
    }
    case '%':
      kv_emit_char(out_buf, &out_len, '%');
      break;
    default:
      kv_emit_char(out_buf, &out_len, '%');
      kv_emit_char(out_buf, &out_len, *p);
      break;
    }
  }

  if (out_len > 0) {
    console_write(out_buf, out_len);
  }
}

void kprintf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  kvprintf(fmt, args);
  va_end(args);
}
