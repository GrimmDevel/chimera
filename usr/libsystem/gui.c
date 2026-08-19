// gui client lib helpers
#include <gui.h>
#include <windowserver.h>
#include <bootstrap.h>
#include <font8x16.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>

extern int mach_msg_trap(void *msg, int option, unsigned int send_size, unsigned int rcv_size,
                         unsigned int rcv_name, unsigned int timeout, unsigned int notify);
extern mach_port_t mach_task_self(void);
extern int mach_port_allocate(unsigned int task, unsigned int right, unsigned int *name);
extern int mach_port_deallocate(unsigned int task, unsigned int name);

gui_window_t *gui_create_window(int width, int height, const char *title) {
    mach_port_t server_port = 0;
    for (int retry = 0; retry < 15; retry++) {
        if (bootstrap_look_up(bootstrap_port, WS_BOOTSTRAP_NAME, &server_port) == BOOTSTRAP_SUCCESS) {
            break;
        }
        usleep(20000);
    }
    if (!server_port) {
        return NULL;
    }

    mach_port_t event_port = 0;
    if (mach_port_allocate(mach_task_self(), 1, &event_port) != 0) {
        return NULL;
    }

    mach_port_t reply_port = 0;
    mach_port_allocate(mach_task_self(), 1, &reply_port);

    ws_req_create_t req;
    memset(&req, 0, sizeof(req));
    req.msgh_size = sizeof(req);
    req.msgh_remote_port = server_port;
    req.msgh_local_port = reply_port;
    req.msgh_id = WS_MSG_CREATE_WINDOW;
    req.width = width;
    req.height = height;
    req.flags = event_port;
    if (title) strncpy(req.title, title, sizeof(req.title) - 1);

    int rc = mach_msg_trap(&req, 1, sizeof(req), 0, 0, 1000, 0);
    if (rc != 0) {
        mach_port_deallocate(mach_task_self(), reply_port);
        mach_port_deallocate(mach_task_self(), event_port);
        return NULL;
    }

    ws_rep_create_t rep;
    memset(&rep, 0, sizeof(rep));
    rc = mach_msg_trap(&rep, 2, 0, sizeof(rep) + 64, reply_port, 1000, 0);
    mach_port_deallocate(mach_task_self(), reply_port);

    if (rc != 0 || rep.ret_code != 0) {
        mach_port_deallocate(mach_task_self(), event_port);
        return NULL;
    }

    gui_window_t *win = (gui_window_t *)malloc(sizeof(gui_window_t));
    if (!win) return NULL;

    win->window_id = rep.window_id;
    win->width = width;
    win->height = height;
    win->stride = width;
    win->server_port = server_port;
    win->event_port = event_port;

    // local backbuffer
    size_t surf_sz = ((width * height * sizeof(unsigned int) + 4095) / 4096) * 4096;
    win->pixels = (unsigned int *)malloc(surf_sz);
    if (!win->pixels) {
        free(win);
        return NULL;
    }
    memset(win->pixels, 0xFF, surf_sz);

    // shared surface mmap
    if (rep.surface_addr != 0) {
        win->surface_pixels = (unsigned int *)mmap((void *)rep.surface_addr, surf_sz, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    } else {
        win->surface_pixels = NULL;
    }

    return win;
}

void gui_destroy_window(gui_window_t *win) {
    if (!win) return;
    ws_msg_destroy_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.msgh_size = sizeof(msg);
    msg.msgh_remote_port = win->server_port;
    msg.msgh_local_port = 0;
    msg.msgh_id = WS_MSG_DESTROY_WINDOW;
    msg.window_id = win->window_id;
    mach_msg_trap(&msg, 1, sizeof(msg), 0, 0, 1000, 0);

    if (win->surface_pixels && win->surface_pixels != (void *)-1) {
        size_t surf_sz = ((win->width * win->height * sizeof(unsigned int) + 4095) / 4096) * 4096;
        munmap(win->surface_pixels, surf_sz);
    }
    if (win->pixels) free(win->pixels);
    if (win->event_port) mach_port_deallocate(mach_task_self(), win->event_port);
    free(win);
}

void gui_update_rect(gui_window_t *win, int x, int y, int w, int h) {
    if (!win || !win->pixels) return;

    if (win->surface_pixels && win->surface_pixels != (void *)-1) {
        size_t surf_sz = win->width * win->height * sizeof(unsigned int);
        memcpy(win->surface_pixels, win->pixels, surf_sz);
    }

    ws_msg_update_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.msgh_size = sizeof(msg);
    msg.msgh_remote_port = win->server_port;
    msg.msgh_local_port = 0;
    msg.msgh_id = WS_MSG_UPDATE_WINDOW;
    msg.window_id = win->window_id;
    msg.dirty_x = x;
    msg.dirty_y = y;
    msg.dirty_w = w;
    msg.dirty_h = h;
    mach_msg_trap(&msg, 1, sizeof(msg), 0, 0, 100, 0);
}

void gui_update(gui_window_t *win) {
    if (!win) return;
    gui_update_rect(win, 0, 0, win->width, win->height);
}

int gui_poll_event(gui_window_t *win, ws_event_t *ev) {
    if (!win || !ev || !win->event_port) return 0;
    ws_event_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    int rc = mach_msg_trap(&msg, 2, 0, sizeof(msg) + 64, win->event_port, 0, 0);
    if (rc == 0 && msg.event_type != WS_EVENT_NONE) {
        ev->type = msg.event_type;
        ev->x = msg.mouse_x;
        ev->y = msg.mouse_y;
        ev->button = msg.button;
        ev->key = msg.key;
        ev->modifiers = msg.modifiers;
        return 1;
    }
    return 0;
}

void gui_clear(gui_window_t *win, unsigned int color) {
    if (!win || !win->pixels) return;
    int total = win->width * win->height;
    for (int i = 0; i < total; i++) {
        win->pixels[i] = color;
    }
}

void gui_set_pixel(gui_window_t *win, int x, int y, unsigned int color) {
    if (!win || !win->pixels || x < 0 || x >= win->width || y < 0 || y >= win->height) return;
    win->pixels[y * win->stride + x] = color;
}

void gui_fill_rect(gui_window_t *win, int x, int y, int w, int h, unsigned int color) {
    if (!win || !win->pixels) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > win->width) ? win->width : (x + w);
    int y1 = (y + h > win->height) ? win->height : (y + h);

    for (int py = y0; py < y1; py++) {
        unsigned int *row = &win->pixels[py * win->stride + x0];
        for (int px = x0; px < x1; px++) {
            *row++ = color;
        }
    }
}

void gui_draw_rect(gui_window_t *win, int x, int y, int w, int h, unsigned int color) {
    gui_fill_rect(win, x, y, w, 1, color);
    gui_fill_rect(win, x, y + h - 1, w, 1, color);
    gui_fill_rect(win, x, y, 1, h, color);
    gui_fill_rect(win, x + w - 1, y, 1, h, color);
}

static inline int gui_abs(int v) { return v < 0 ? -v : v; }

void gui_draw_line(gui_window_t *win, int x0, int y0, int x1, int y1, unsigned int color) {
    int dx = gui_abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -gui_abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    for (;;) {
        gui_set_pixel(win, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gui_draw_char(gui_window_t *win, int x, int y, char c, unsigned int color) {
    if (!win || !win->pixels) return;
    unsigned char uc = (unsigned char)c;
    if (uc > 127) uc = '?';
    const unsigned char *glyph = g_font8x16[uc];

    for (int row = 0; row < 16; row++) {
        int py = y + row;
        if (py < 0 || py >= win->height) continue;
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px < 0 || px >= win->width) continue;
            if (bits & (0x80 >> col)) {
                win->pixels[py * win->stride + px] = color;
            }
        }
    }
}

void gui_draw_text(gui_window_t *win, int x, int y, const char *text, unsigned int color) {
    if (!win || !text) return;
    int cx = x;
    while (*text) {
        if (*text == '\n') {
            cx = x;
            y += 16;
        } else {
            gui_draw_char(win, cx, y, *text, color);
            cx += 8;
        }
        text++;
    }
}

void gui_draw_button(gui_window_t *win, int x, int y, int w, int h, const char *text, int is_pressed) {
    unsigned int bg_color = is_pressed ? GUI_COLOR_MID_GRAY : GUI_COLOR_LIGHT_GRAY;
    unsigned int border_color = is_pressed ? GUI_COLOR_BLACK : GUI_COLOR_DARK_GRAY;
    unsigned int text_color = is_pressed ? GUI_COLOR_WHITE : GUI_COLOR_BLACK;

    gui_fill_rect(win, x + 1, y + 1, w - 2, h - 2, bg_color);
    gui_draw_rect(win, x, y, w, h, border_color);

    if (text) {
        int text_len = (int)strlen(text);
        int text_x = x + (w - text_len * 8) / 2;
        int text_y = y + (h - 16) / 2;
        gui_draw_text(win, text_x, text_y, text, text_color);
    }
}
