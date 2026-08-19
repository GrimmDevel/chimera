// gui client framework header
#pragma once
#ifndef _GUI_H_
#define _GUI_H_

#include <windowserver.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GUI_RGB(r, g, b)        (((unsigned int)(r) << 16) | ((unsigned int)(g) << 8) | (unsigned int)(b))
#define GUI_ARGB(a, r, g, b)    (((unsigned int)(a) << 24) | ((unsigned int)(r) << 16) | ((unsigned int)(g) << 8) | (unsigned int)(b))

#define GUI_COLOR_BLACK         0x00000000
#define GUI_COLOR_WHITE         0x00FFFFFF
#define GUI_COLOR_DARK_GRAY     0x002D3037
#define GUI_COLOR_MID_GRAY      0x004A505C
#define GUI_COLOR_LIGHT_GRAY    0x00D0D5DD
#define GUI_COLOR_BG_GRAY       0x00F2F4F7
#define GUI_COLOR_BLUE          0x00007AFF
#define GUI_COLOR_CYAN          0x0032ADE6
#define GUI_COLOR_GREEN         0x0034C759
#define GUI_COLOR_RED           0x00FF3B30
#define GUI_COLOR_YELLOW        0x00FFCC00
#define GUI_COLOR_ORANGE        0x00FF9500

typedef struct {
    unsigned int        window_id;
    int                 width;
    int                 height;
    int                 stride;
    unsigned int       *pixels;
    unsigned int       *surface_pixels;
    mach_port_t         server_port;
    mach_port_t         event_port;
} gui_window_t;

gui_window_t *gui_create_window(int width, int height, const char *title);
void          gui_destroy_window(gui_window_t *win);
void          gui_update(gui_window_t *win);
void          gui_update_rect(gui_window_t *win, int x, int y, int w, int h);
int           gui_poll_event(gui_window_t *win, ws_event_t *ev);

void gui_clear(gui_window_t *win, unsigned int color);
void gui_set_pixel(gui_window_t *win, int x, int y, unsigned int color);
void gui_fill_rect(gui_window_t *win, int x, int y, int w, int h, unsigned int color);
void gui_draw_rect(gui_window_t *win, int x, int y, int w, int h, unsigned int color);
void gui_draw_line(gui_window_t *win, int x0, int y0, int x1, int y1, unsigned int color);
void gui_draw_char(gui_window_t *win, int x, int y, char c, unsigned int color);
void gui_draw_text(gui_window_t *win, int x, int y, const char *text, unsigned int color);
void gui_draw_button(gui_window_t *win, int x, int y, int w, int h, const char *text, int is_pressed);

#ifdef __cplusplus
}
#endif

#endif
