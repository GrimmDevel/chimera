#ifndef WS_FONT_H
#define WS_FONT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern const uint8_t ws_font_data[256 * 16];

static inline uint32_t ws_blend_argb(uint32_t bg, uint32_t fg) {
    uint32_t a = (fg >> 24) & 0xFF;
    if (a == 255) return fg;
    if (a == 0) return bg;

    uint32_t inv_a = 255 - a;
    uint32_t r = (((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv_a) / 255;
    uint32_t g = (((fg >> 8)  & 0xFF) * a + ((bg >> 8)  & 0xFF) * inv_a) / 255;
    uint32_t b = ((fg & 0xFF) * a + (bg & 0xFF) * inv_a) / 255;
    return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

static inline void ws_put_pixel(uint32_t *p, int pitch, int w, int h, int x, int y, uint32_t color) {
    if (x < 0 || x >= w || y < 0 || y >= h || !p) return;
    if (((color >> 24) & 0xFF) == 255) {
        p[y * pitch + x] = color;
    } else {
        p[y * pitch + x] = ws_blend_argb(p[y * pitch + x], color);
    }
}

static inline void ws_fill_rect(uint32_t *p, int pitch, int w, int h, int rx, int ry, int rw, int rh, uint32_t color) {
    if (!p || rw <= 0 || rh <= 0) return;
    int x0 = rx < 0 ? 0 : rx;
    int y0 = ry < 0 ? 0 : ry;
    int x1 = (rx + rw > w) ? w : rx + rw;
    int y1 = (ry + rh > h) ? h : ry + rh;

    uint32_t alpha = (color >> 24) & 0xFF;
    for (int y = y0; y < y1; y++) {
        uint32_t *row = &p[y * pitch];
        if (alpha == 255) {
            for (int x = x0; x < x1; x++) {
                row[x] = color;
            }
        } else {
            for (int x = x0; x < x1; x++) {
                row[x] = ws_blend_argb(row[x], color);
            }
        }
    }
}

static inline void ws_draw_gradient_v(uint32_t *p, int pitch, int w, int h, int rx, int ry, int rw, int rh, uint32_t col_top, uint32_t col_bot) {
    if (!p || rw <= 0 || rh <= 0) return;
    int x0 = rx < 0 ? 0 : rx;
    int y0 = ry < 0 ? 0 : ry;
    int x1 = (rx + rw > w) ? w : rx + rw;
    int y1 = (ry + rh > h) ? h : ry + rh;

    int tr = (col_top >> 16) & 0xFF, tg = (col_top >> 8) & 0xFF, tb = col_top & 0xFF;
    int br = (col_bot >> 16) & 0xFF, bg = (col_bot >> 8) & 0xFF, bb = col_bot & 0xFF;

    for (int y = y0; y < y1; y++) {
        int t = (y - ry) * 256 / rh;
        int r = tr + ((br - tr) * t) / 256;
        int g = tg + ((bg - tg) * t) / 256;
        int b = tb + ((bb - tb) * t) / 256;
        uint32_t c = (0xFF << 24) | (r << 16) | (g << 8) | b;

        uint32_t *row = &p[y * pitch];
        for (int x = x0; x < x1; x++) {
            row[x] = c;
        }
    }
}

static inline void ws_draw_rounded_rect(uint32_t *p, int pitch, int w, int h, int rx, int ry, int rw, int rh, int radius, uint32_t fill_color, uint32_t border_color) {
    if (!p || rw <= 0 || rh <= 0) return;
    int x0 = rx < 0 ? 0 : rx;
    int y0 = ry < 0 ? 0 : ry;
    int x1 = (rx + rw > w) ? w : rx + rw;
    int y1 = (ry + rh > h) ? h : ry + rh;
    int r2 = radius * radius;

    for (int y = y0; y < y1; y++) {
        int dy = 0;
        if (y < ry + radius) dy = ry + radius - y;
        else if (y >= ry + rh - radius) dy = y - (ry + rh - radius - 1);

        for (int x = x0; x < x1; x++) {
            int dx = 0;
            if (x < rx + radius) dx = rx + radius - x;
            else if (x >= rx + rw - radius) dx = x - (rx + rw - radius - 1);

            int dist2 = dx * dx + dy * dy;
            if (dist2 > r2) continue;

            bool is_border = (border_color != 0) && (
                x == rx || x == rx + rw - 1 || y == ry || y == ry + rh - 1 ||
                (dist2 > (radius - 1) * (radius - 1))
            );

            if (is_border) {
                ws_put_pixel(p, pitch, w, h, x, y, border_color);
            } else if (fill_color != 0) {
                ws_put_pixel(p, pitch, w, h, x, y, fill_color);
            }
        }
    }
}

static inline void ws_draw_circle(uint32_t *p, int pitch, int w, int h, int cx, int cy, int radius, uint32_t fill_color, uint32_t border_color) {
    if (!p || radius <= 0) return;
    int r2 = radius * radius;
    int r_in2 = (radius - 1) * (radius - 1);

    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int d2 = x * x + y * y;
            if (d2 <= r2) {
                if (border_color != 0 && d2 >= r_in2) {
                    ws_put_pixel(p, pitch, w, h, cx + x, cy + y, border_color);
                } else if (fill_color != 0) {
                    ws_put_pixel(p, pitch, w, h, cx + x, cy + y, fill_color);
                }
            }
        }
    }
}

static inline void ws_draw_glyph(uint32_t *p, int pitch, int w, int h, int gx, int gy, char c, uint32_t color) {
    unsigned char uc = (unsigned char)c;
    const uint8_t *glyph = &ws_font_data[uc * 16];

    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
                ws_put_pixel(p, pitch, w, h, gx + col, gy + row, color);
            }
        }
    }
}

static inline void ws_draw_text(uint32_t *p, int pitch, int w, int h, int x, int y, const char *str, uint32_t color) {
    if (!p || !str) return;
    int cur_x = x;
    while (*str) {
        if (*str == '\n') {
            y += 16;
            cur_x = x;
        } else {
            ws_draw_glyph(p, pitch, w, h, cur_x, y, *str, color);
            cur_x += 8;
        }
        str++;
    }
}

static inline void ws_draw_text_bold(uint32_t *p, int pitch, int w, int h, int x, int y, const char *str, uint32_t color) {
    if (!p || !str) return;
    int cur_x = x;
    while (*str) {
        if (*str == '\n') {
            y += 16;
            cur_x = x;
        } else {
            ws_draw_glyph(p, pitch, w, h, cur_x, y, *str, color);
            ws_draw_glyph(p, pitch, w, h, cur_x + 1, y, *str, color);
            cur_x += 9;
        }
        str++;
    }
}

static inline void ws_draw_traffic_lights(uint32_t *p, int pitch, int w, int h, int x, int y, int hover_type) {
    (void)hover_type;
    ws_draw_circle(p, pitch, w, h, x + 6,  y + 6, 6, 0xFFFF5F56, 0xFFE0443E);
    ws_draw_circle(p, pitch, w, h, x + 24, y + 6, 6, 0xFFFFBD2E, 0xFFDEA123);
    ws_draw_circle(p, pitch, w, h, x + 42, y + 6, 6, 0xFF27C93F, 0xFF1AAB29);
}

static inline void ws_draw_apple_logo(uint32_t *p, int pitch, int w, int h, int x, int y, uint32_t color) {
    static const char *logo[14] = {
        "   ..   ",
        "    ..  ",
        "  ......",
        " ........",
        ".........",
        ".........",
        ".........",
        ".........",
        ".........",
        " ........",
        "  ......",
        "  ..  ..",
        "   .  . ",
        "        "
    };
    for (int r = 0; r < 14; r++) {
        for (int c = 0; c < 9; c++) {
            if (logo[r][c] == '.') {
                ws_put_pixel(p, pitch, w, h, x + c, y + r, color);
            }
        }
    }
}

#endif
