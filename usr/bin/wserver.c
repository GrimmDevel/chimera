// windowserver compositor daemon
#include <bootstrap.h>
#include <fcntl.h>
#include <input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <windowserver.h>

#define FBIOGET_INFO 0x4601
#define FBIOPAN_DISPLAY 0x4602

struct fb_info {
  unsigned int width;
  unsigned int height;
  unsigned int pitch;
  unsigned int bpp;
  unsigned int format;
  unsigned int vram_size;
};

struct fb_pan_info {
  unsigned int xoffset;
  unsigned int yoffset;
};

extern int mach_msg_trap(void *msg, int option, unsigned int send_size,
                         unsigned int rcv_size, unsigned int rcv_name,
                         unsigned int timeout, unsigned int notify);
extern mach_port_t mach_task_self(void);
extern int mach_port_allocate(unsigned int task, unsigned int right,
                              unsigned int *name);
extern int mach_port_deallocate(unsigned int task, unsigned int name);

typedef struct {
  unsigned int window_id;
  int x;
  int y;
  int width;
  int height;
  char title[WS_MAX_TITLE_LEN];
  unsigned int *pixels;
  mach_port_t event_port;
  int is_visible;
  int is_focused;
} ws_window_t;

static int s_fb_fd = -1;
static int s_mouse_fd = -1;
static unsigned int s_screen_w = 1280;
static unsigned int s_screen_h = 800;
static unsigned int s_screen_pitch = 1280;
static unsigned int *s_frontbuffer = NULL;
static unsigned int *s_backbuffer = NULL;

static ws_window_t s_windows[WS_MAX_WINDOWS];
static int s_window_count = 0;
static unsigned int s_next_window_id = 1;

static int s_cursor_x = 640;
static int s_cursor_y = 400;
static int s_prev_buttons = 0;

static int s_drag_win_id = -1;
static int s_drag_offset_x = 0;
static int s_drag_offset_y = 0;

static int s_damage_min_x = 0;
static int s_damage_min_y = 0;
static int s_damage_max_x = 1280;
static int s_damage_max_y = 800;
static int s_has_damage = 1;

static void mark_damage(int x, int y, int w, int h) {
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > (int)s_screen_w)
    w = s_screen_w - x;
  if (y + h > (int)s_screen_h)
    h = s_screen_h - y;
  if (w <= 0 || h <= 0)
    return;

  if (!s_has_damage) {
    s_damage_min_x = x;
    s_damage_min_y = y;
    s_damage_max_x = x + w;
    s_damage_max_y = y + h;
    s_has_damage = 1;
  } else {
    if (x < s_damage_min_x)
      s_damage_min_x = x;
    if (y < s_damage_min_y)
      s_damage_min_y = y;
    if (x + w > s_damage_max_x)
      s_damage_max_x = x + w;
    if (y + h > s_damage_max_y)
      s_damage_max_y = y + h;
  }
}

#include <font8x16.h>

static void draw_char_bb(int x, int y, char c, unsigned int color) {
  unsigned char uc = (unsigned char)c;
  if (uc > 127)
    uc = '?';
  const unsigned char *glyph = g_font8x16[uc];

  for (int row = 0; row < 16; row++) {
    int py = y + row;
    if (py < 0 || py >= (int)s_screen_h)
      continue;
    unsigned char bits = glyph[row];
    for (int col = 0; col < 8; col++) {
      int px = x + col;
      if (px < 0 || px >= (int)s_screen_w)
        continue;
      if (bits & (0x80 >> col)) {
        s_backbuffer[py * s_screen_pitch + px] = color;
      }
    }
  }
}

static void draw_text_bb(int x, int y, const char *text, unsigned int color) {
  int cx = x;
  while (*text) {
    if (*text == '\n') {
      cx = x;
      y += 16;
    } else {
      draw_char_bb(cx, y, *text, color);
      cx += 8;
    }
    text++;
  }
}

static void fill_rect_bb(int x, int y, int w, int h, unsigned int color) {
  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = (x + w > (int)s_screen_w) ? (int)s_screen_w : (x + w);
  int y1 = (y + h > (int)s_screen_h) ? (int)s_screen_h : (y + h);

  for (int py = y0; py < y1; py++) {
    unsigned int *row = &s_backbuffer[py * s_screen_pitch + x0];
    for (int px = x0; px < x1; px++) {
      *row++ = color;
    }
  }
}

static void draw_circle_bb(int cx, int cy, int r, unsigned int color) {
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      if (dx * dx + dy * dy <= r * r) {
        int px = cx + dx;
        int py = cy + dy;
        if (px >= 0 && px < (int)s_screen_w && py >= 0 &&
            py < (int)s_screen_h) {
          s_backbuffer[py * s_screen_pitch + px] = color;
        }
      }
    }
  }
}

static unsigned int *s_wallpaper = NULL;

static void init_wallpaper(void) {
  s_wallpaper = (unsigned int *)malloc(s_screen_pitch * s_screen_h *
                                       sizeof(unsigned int));
  if (!s_wallpaper)
    return;

  unsigned int *saved = s_backbuffer;
  s_backbuffer = s_wallpaper;

  // background gradient
  for (unsigned int y = 24; y < s_screen_h; y++) {
    unsigned int r = 11 + (y * 15 / s_screen_h);
    unsigned int g = 25 + (y * 35 / s_screen_h);
    unsigned int b = 44 + (y * 65 / s_screen_h);
    unsigned int col = (r << 16) | (g << 8) | b;
    unsigned int *row = &s_wallpaper[y * s_screen_pitch];
    for (unsigned int x = 0; x < s_screen_w; x++) {
      *row++ = col;
    }
  }

  // top bar
  fill_rect_bb(0, 0, s_screen_w, 24, 0x001A1D24);
  draw_text_bb(12, 4, "XIU OS", 0x00FFFFFF);
  draw_text_bb(84, 4, "File", 0x00D0D5DD);
  draw_text_bb(132, 4, "Edit", 0x00D0D5DD);
  draw_text_bb(180, 4, "View", 0x00D0D5DD);
  draw_text_bb(228, 4, "Window", 0x00D0D5DD);
  draw_text_bb(292, 4, "Help", 0x00D0D5DD);
  draw_text_bb(s_screen_w - 280, 4, "SMP: 4 Cores | RAM: 4096M", 0x0098A2B3);

  // bottom dock
  int dock_w = 400;
  int dock_h = 40;
  int dock_x = (s_screen_w - dock_w) / 2;
  int dock_y = s_screen_h - dock_h - 10;
  fill_rect_bb(dock_x, dock_y, dock_w, dock_h, 0x00242832);
  fill_rect_bb(dock_x + 10, dock_y + 6, 80, 28, 0x00007AFF);
  draw_text_bb(dock_x + 22, dock_y + 12, "SysMon", 0x00FFFFFF);

  fill_rect_bb(dock_x + 105, dock_y + 6, 80, 28, 0x0034C759);
  draw_text_bb(dock_x + 128, dock_y + 12, "Calc", 0x00FFFFFF);

  fill_rect_bb(dock_x + 200, dock_y + 6, 80, 28, 0x00FF9500);
  draw_text_bb(dock_x + 215, dock_y + 12, "Terminal", 0x00FFFFFF);

  fill_rect_bb(dock_x + 295, dock_y + 6, 95, 28, 0x005856D6);
  draw_text_bb(dock_x + 305, dock_y + 12, "WindowServer", 0x00FFFFFF);

  s_backbuffer = saved;
}

static void render_desktop_region(int min_y, int max_y) {
  if (!s_wallpaper)
    return;
  if (min_y < 0)
    min_y = 0;
  if (max_y > (int)s_screen_h)
    max_y = s_screen_h;
  if (min_y >= max_y)
    return;
  size_t off = min_y * s_screen_pitch;
  size_t sz = (max_y - min_y) * s_screen_pitch * sizeof(unsigned int);
  memcpy(&s_backbuffer[off], &s_wallpaper[off], sz);
}

static void render_windows(void) {
  for (int i = 0; i < s_window_count; i++) {
    ws_window_t *win = &s_windows[i];
    if (!win->is_visible)
      continue;

    int wx = win->x;
    int wy = win->y;
    int ww = win->width;
    int wh = win->height;
    int title_h = 26;

    // shadow + frame
    fill_rect_bb(wx + 4, wy + 4, ww, wh + title_h, 0x0005080E);

    unsigned int title_bg = win->is_focused ? 0x002B303C : 0x001E222A;
    fill_rect_bb(wx, wy, ww, title_h, title_bg);

    draw_circle_bb(wx + 14, wy + 13, 5, 0x00FF5F56);
    draw_circle_bb(wx + 30, wy + 13, 5, 0x00FFBD2E);
    draw_circle_bb(wx + 46, wy + 13, 5, 0x0027C93F);

    int tlen = (int)strlen(win->title);
    int tx = wx + (ww - tlen * 8) / 2;
    draw_text_bb(tx, wy + 5, win->title, 0x00FFFFFF);

    if (win->pixels) {
      for (int py = 0; py < wh; py++) {
        int screen_y = wy + title_h + py;
        if (screen_y < 24 || screen_y >= (int)s_screen_h)
          continue;

        unsigned int *src_row = &win->pixels[py * ww];
        for (int px = 0; px < ww; px++) {
          int screen_x = wx + px;
          if (screen_x >= 0 && screen_x < (int)s_screen_w) {
            s_backbuffer[screen_y * s_screen_pitch + screen_x] = src_row[px];
          }
        }
      }
    }
  }
}

// 12x18 arrow cursor
static const unsigned short s_cursor_mask[18] = {
    0b1000000000000000, 0b1100000000000000, 0b1110000000000000,
    0b1111000000000000, 0b1111100000000000, 0b1111110000000000,
    0b1111111000000000, 0b1111111100000000, 0b1111111110000000,
    0b1111111111000000, 0b1111110000000000, 0b1101111000000000,
    0b1000111100000000, 0b0000011110000000, 0b0000001111000000,
    0b0000000111000000, 0b0000000011000000, 0b0000000000000000,
};

static int s_rendered_cursor_x = 0;
static int s_rendered_cursor_y = 0;
static int s_cursor_rendered = 0;

static void erase_cursor(void) {
  if (!s_cursor_rendered)
    return;
  for (int dy = 0; dy < 18; dy++) {
    int py = s_rendered_cursor_y + dy;
    if (py < 0 || py >= (int)s_screen_h)
      continue;
    int px = s_rendered_cursor_x;
    int count = 16;
    if (px < 0) {
      count += px;
      px = 0;
    }
    if (px + count > (int)s_screen_w)
      count = s_screen_w - px;
    if (count > 0) {
      size_t off = py * s_screen_pitch + px;
      memcpy(&s_frontbuffer[off], &s_backbuffer[off],
             count * sizeof(unsigned int));
    }
  }
  s_cursor_rendered = 0;
}

static void update_cursor(int new_x, int new_y) {
  if (!s_cursor_rendered) {
    for (int dy = 0; dy < 18; dy++) {
      int py = new_y + dy;
      if (py < 0 || py >= (int)s_screen_h)
        continue;
      unsigned short row_mask = s_cursor_mask[dy];
      unsigned int line_buf[16];
      int px = new_x;
      int count = 16;
      if (px < 0) {
        count += px;
        px = 0;
      }
      if (px + count > (int)s_screen_w)
        count = s_screen_w - px;
      if (count <= 0)
        continue;

      size_t off = py * s_screen_pitch + px;
      memcpy(line_buf, &s_backbuffer[off], count * sizeof(unsigned int));

      for (int dx = 0; dx < count; dx++) {
        int bit_idx = (px - new_x) + dx;
        if (bit_idx >= 0 && bit_idx < 16) {
          if (row_mask & (0x8000 >> bit_idx)) {
            unsigned int col = (bit_idx == 0 || dy == 0 ||
                                (row_mask & (0x4000 >> bit_idx)) == 0)
                                   ? 0x00000000
                                   : 0x00FFFFFF;
            line_buf[dx] = col;
          }
        }
      }
      memcpy(&s_frontbuffer[off], line_buf, count * sizeof(unsigned int));
    }
    s_rendered_cursor_x = new_x;
    s_rendered_cursor_y = new_y;
    s_cursor_rendered = 1;
    return;
  }

  if (s_rendered_cursor_x == new_x && s_rendered_cursor_y == new_y) {
    return;
  }

  int old_x = s_rendered_cursor_x;
  int old_y = s_rendered_cursor_y;

  int min_x = old_x < new_x ? old_x : new_x;
  int max_x = (old_x + 16 > new_x + 16) ? (old_x + 16) : (new_x + 16);
  int min_y = old_y < new_y ? old_y : new_y;
  int max_y = (old_y + 18 > new_y + 18) ? (old_y + 18) : (new_y + 18);

  if (max_x - min_x > 64 || max_y - min_y > 64) {
    erase_cursor();
    update_cursor(new_x, new_y);
    return;
  }

  if (min_x < 0)
    min_x = 0;
  if (max_x > (int)s_screen_w)
    max_x = s_screen_w;
  if (min_y < 0)
    min_y = 0;
  if (max_y > (int)s_screen_h)
    max_y = s_screen_h;

  int w = max_x - min_x;
  if (w <= 0 || min_y >= max_y) {
    s_rendered_cursor_x = new_x;
    s_rendered_cursor_y = new_y;
    return;
  }

  unsigned int line_buf[64];

  for (int py = min_y; py < max_y; py++) {
    size_t off = py * s_screen_pitch + min_x;
    memcpy(line_buf, &s_backbuffer[off], w * sizeof(unsigned int));

    if (py >= new_y && py < new_y + 18) {
      int dy = py - new_y;
      unsigned short row_mask = s_cursor_mask[dy];
      for (int dx = 0; dx < 16; dx++) {
        int px = new_x + dx;
        if (px >= min_x && px < max_x) {
          if (row_mask & (0x8000 >> dx)) {
            unsigned int col =
                (dx == 0 || dy == 0 || (row_mask & (0x4000 >> dx)) == 0)
                    ? 0x00000000
                    : 0x00FFFFFF;
            line_buf[px - min_x] = col;
          }
        }
      }
    }

    memcpy(&s_frontbuffer[off], line_buf, w * sizeof(unsigned int));
  }

  s_rendered_cursor_x = new_x;
  s_rendered_cursor_y = new_y;
  s_cursor_rendered = 1;
}

static void send_client_event(mach_port_t event_port, unsigned int type, int x,
                              int y, unsigned int button) {
  if (!event_port)
    return;
  ws_event_msg_t msg;
  memset(&msg, 0, sizeof(msg));
  msg.msgh_size = sizeof(msg);
  msg.msgh_remote_port = event_port;
  msg.msgh_local_port = 0;
  msg.event_type = type;
  msg.mouse_x = x;
  msg.mouse_y = y;
  msg.button = button;
  mach_msg_trap(&msg, 1, sizeof(msg), 0, 0, 10, 0);
}

static int handle_mouse_input(void) {
  if (s_mouse_fd < 0)
    return 0;

  int moved = 0;
  int old_cx = s_cursor_x;
  int old_cy = s_cursor_y;
  xiu_event_t ev;
  while (read(s_mouse_fd, &ev, sizeof(ev)) == sizeof(ev)) {
    if (ev.data.mouse.delta_x != 0 || ev.data.mouse.delta_y != 0 ||
        ev.data.mouse.buttons != s_prev_buttons) {
      moved = 1;
    }
    s_cursor_x += ev.data.mouse.delta_x;
    s_cursor_y += ev.data.mouse.delta_y;
    int buttons = (int)ev.data.mouse.buttons;

    if (s_cursor_x < 0)
      s_cursor_x = 0;
    if (s_cursor_x >= (int)s_screen_w)
      s_cursor_x = s_screen_w - 1;
    if (s_cursor_y < 0)
      s_cursor_y = 0;
    if (s_cursor_y >= (int)s_screen_h)
      s_cursor_y = s_screen_h - 1;

    int left_pressed = (buttons & 1) && !(s_prev_buttons & 1);
    int left_released = !(buttons & 1) && (s_prev_buttons & 1);

    if (left_pressed) {
      // hit testing top to bottom
      s_drag_win_id = -1;
      for (int i = s_window_count - 1; i >= 0; i--) {
        ws_window_t *w = &s_windows[i];
        if (!w->is_visible)
          continue;

        // close button
        if (s_cursor_x >= w->x + 8 && s_cursor_x <= w->x + 20 &&
            s_cursor_y >= w->y + 7 && s_cursor_y <= w->y + 19) {
          send_client_event(w->event_port, WS_EVENT_WINDOW_CLOSE, 0, 0, 1);
          break;
        }

        // titlebar drag
        if (s_cursor_x >= w->x && s_cursor_x <= w->x + w->width &&
            s_cursor_y >= w->y && s_cursor_y <= w->y + 26) {
          s_drag_win_id = w->window_id;
          s_drag_offset_x = s_cursor_x - w->x;
          s_drag_offset_y = s_cursor_y - w->y;

          // bring to front
          ws_window_t tmp = *w;
          for (int j = i; j < s_window_count - 1; j++)
            s_windows[j] = s_windows[j + 1];
          s_windows[s_window_count - 1] = tmp;
          mark_damage(w->x - 4, w->y - 4, w->width + 12, w->height + 36);
          break;
        }

        // client area click
        if (s_cursor_x >= w->x && s_cursor_x <= w->x + w->width &&
            s_cursor_y >= w->y + 26 && s_cursor_y <= w->y + 26 + w->height) {
          int cx = s_cursor_x - w->x;
          int cy = s_cursor_y - (w->y + 26);
          send_client_event(w->event_port, WS_EVENT_MOUSE_DOWN, cx, cy, 1);
          break;
        }
      }
    } else if (left_released) {
      s_drag_win_id = -1;
    }

    if (s_drag_win_id > 0) {
      for (int i = 0; i < s_window_count; i++) {
        if (s_windows[i].window_id == (unsigned int)s_drag_win_id) {
          mark_damage(s_windows[i].x - 4, s_windows[i].y - 4,
                      s_windows[i].width + 12, s_windows[i].height + 36);
          s_windows[i].x = s_cursor_x - s_drag_offset_x;
          s_windows[i].y = s_cursor_y - s_drag_offset_y;
          mark_damage(s_windows[i].x - 4, s_windows[i].y - 4,
                      s_windows[i].width + 12, s_windows[i].height + 36);
          break;
        }
      }
    }

    s_prev_buttons = buttons;
  }

  if (s_cursor_x != old_cx || s_cursor_y != old_cy) {
    moved = 1;
  }
  return moved;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  printf("[WindowServer] Starting Darwin Quartz Compositor Daemon...\n");

  mach_port_t ws_server_port = 0;
  mach_port_allocate(mach_task_self(), 1, &ws_server_port);

  if (bootstrap_register(bootstrap_port, WS_BOOTSTRAP_NAME, ws_server_port) !=
      BOOTSTRAP_SUCCESS) {
    printf("[-] WindowServer: bootstrap_register failed\n");
    return 1;
  }

  s_fb_fd = open("/dev/fb0", O_RDWR);
  if (s_fb_fd < 0) {
    printf("[-] WindowServer: cannot open /dev/fb0\n");
    return 1;
  }

  struct fb_info info;
  if (ioctl(s_fb_fd, FBIOGET_INFO, &info) == 0 && info.width > 0) {
    s_screen_w = info.width;
    s_screen_h = info.height;
    if (info.pitch >= info.width * 4) {
      s_screen_pitch = info.pitch / 4;
    } else if (info.pitch > 0) {
      s_screen_pitch = info.pitch;
    } else {
      s_screen_pitch = info.width;
    }
  }

  size_t fb_size = s_screen_pitch * s_screen_h * sizeof(unsigned int);
  s_frontbuffer = (unsigned int *)mmap(NULL, fb_size, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, s_fb_fd, 0);
  if (!s_frontbuffer || s_frontbuffer == (void *)-1) {
    printf("[-] WindowServer: mmap /dev/fb0 failed\n");
    return 1;
  }

  s_backbuffer = (unsigned int *)malloc(fb_size);
  if (!s_backbuffer) {
    printf("[-] WindowServer: cannot allocate backbuffer\n");
    return 1;
  }

  init_wallpaper();
  mark_damage(0, 0, s_screen_w, s_screen_h);

  s_mouse_fd = open("/dev/mouse", O_RDONLY | O_NONBLOCK);
  if (s_mouse_fd < 0) {
    printf("[!] WindowServer: mouse device not found, using software cursor\n");
  }

  printf("[WindowServer] Registered '%s' on Mach port 0x%x. Entering render loop (%dx%d)...\n",
         WS_BOOTSTRAP_NAME, ws_server_port, s_screen_w, s_screen_h);

  for (;;) {
    union {
      ws_req_create_t create_req;
      ws_msg_update_t update_msg;
      ws_msg_destroy_t destroy_msg;
      unsigned char raw[256];
    } msg;

    // handle client ipc
    while (mach_msg_trap(&msg, 2, 0, sizeof(msg), ws_server_port, 0, 0) == 0) {
      if (msg.create_req.msgh_id == WS_MSG_CREATE_WINDOW) {
        if (s_window_count < WS_MAX_WINDOWS) {
          ws_window_t *w = &s_windows[s_window_count++];
          memset(w, 0, sizeof(*w));
          w->window_id = s_next_window_id++;
          w->width = msg.create_req.width;
          w->height = msg.create_req.height;
          w->x = 100 + (s_window_count * 30);
          w->y = 60 + (s_window_count * 30);
          strncpy(w->title, msg.create_req.title, sizeof(w->title) - 1);
          w->event_port = msg.create_req.flags;
          w->is_visible = 1;
          w->is_focused = 1;

          size_t surf_sz =
              ((w->width * w->height * sizeof(unsigned int) + 4095) / 4096) * 4096;
          unsigned long long surf_va =
              0xA0000000ULL + ((w->window_id - 1) % 64) * 0x800000ULL;
          w->pixels = (unsigned int *)mmap((void *)surf_va, surf_sz,
                                           PROT_READ | PROT_WRITE,
                                           MAP_ANON | MAP_SHARED, -1, 0);
          if (w->pixels && w->pixels != (void *)-1) {
            memset(w->pixels, 0xEE, surf_sz);
          }

          ws_rep_create_t rep;
          memset(&rep, 0, sizeof(rep));
          rep.msgh_size = sizeof(rep);
          rep.msgh_remote_port = msg.create_req.msgh_remote_port;
          rep.msgh_local_port = 0;
          rep.ret_code = 0;
          rep.window_id = w->window_id;
          rep.event_port = w->event_port;
          rep.surface_addr = surf_va;
          mach_msg_trap(&rep, 1, sizeof(rep), 0, 0, 100, 0);

          mark_damage(w->x - 4, w->y - 4, w->width + 12, w->height + 36);
        }
      } else if (msg.update_msg.msgh_id == WS_MSG_UPDATE_WINDOW) {
        for (int i = 0; i < s_window_count; i++) {
          ws_window_t *w = &s_windows[i];
          if (w->window_id == msg.update_msg.window_id && w->is_visible &&
              w->pixels) {
            int wy = w->y + 26;
            int wx = w->x;
            int ww = w->width;
            int wh = w->height;

            int cursor_in_win =
                (s_cursor_x + 16 >= wx && s_cursor_x <= wx + ww &&
                 s_cursor_y + 18 >= wy && s_cursor_y <= wy + wh);

            for (int py = 0; py < wh; py++) {
              int sy = wy + py;
              if (sy < 24 || sy >= (int)s_screen_h)
                continue;
              int sx = wx;
              int copy_len = ww;
              if (sx < 0) {
                copy_len += sx;
                sx = 0;
              }
              if (sx + copy_len > (int)s_screen_w)
                copy_len = s_screen_w - sx;
              if (copy_len > 0) {
                size_t off = sy * s_screen_pitch + sx;
                memcpy(&s_backbuffer[off], &w->pixels[py * ww],
                       copy_len * sizeof(unsigned int));
                memcpy(&s_frontbuffer[off], &s_backbuffer[off],
                       copy_len * sizeof(unsigned int));
              }
            }

            if (cursor_in_win) {
              s_cursor_rendered = 0;
              update_cursor(s_cursor_x, s_cursor_y);
            }
            break;
          }
        }
      } else if (msg.destroy_msg.msgh_id == WS_MSG_DESTROY_WINDOW) {
        for (int i = 0; i < s_window_count; i++) {
          if (s_windows[i].window_id == msg.destroy_msg.window_id) {
            mark_damage(s_windows[i].x - 4, s_windows[i].y - 4,
                        s_windows[i].width + 12, s_windows[i].height + 36);
            if (s_windows[i].pixels && s_windows[i].pixels != (void *)-1) {
              size_t surf_sz = ((s_windows[i].width * s_windows[i].height *
                                     sizeof(unsigned int) + 4095) / 4096) * 4096;
              munmap(s_windows[i].pixels, surf_sz);
            }
            for (int j = i; j < s_window_count - 1; j++)
              s_windows[j] = s_windows[j + 1];
            s_window_count--;
            break;
          }
        }
      }
    }

    int cursor_moved = handle_mouse_input();

    // blit damaged region
    if (s_has_damage) {
      erase_cursor();

      int min_y = s_damage_min_y;
      int max_y = s_damage_max_y;
      if (min_y < 0)
        min_y = 0;
      if (max_y > (int)s_screen_h)
        max_y = s_screen_h;

      if (max_y > min_y) {
        render_desktop_region(min_y, max_y);
        render_windows();

        size_t offset = min_y * s_screen_pitch;
        size_t count = (max_y - min_y) * s_screen_pitch;
        memcpy(&s_frontbuffer[offset], &s_backbuffer[offset],
               count * sizeof(unsigned int));
      }

      s_has_damage = 0;
      update_cursor(s_cursor_x, s_cursor_y);
    } else if (cursor_moved) {
      update_cursor(s_cursor_x, s_cursor_y);
    }

    usleep(2000);
  }

  return 0;
}
