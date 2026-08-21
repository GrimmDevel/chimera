#include <kernel/video_console.h>
#include <kernel/spinlock.h>
#include <kernel/xiu_types.h>

#ifndef boolean_t
typedef bool boolean_t;
#endif
#ifndef TRUE
#define TRUE true
#endif
#ifndef FALSE
#define FALSE false
#endif

/*
 * Generic Console (Front-End)
 * ---------------------------
 */

struct vc_info vinfo;

static boolean_t gc_initialized = FALSE;
static boolean_t gc_enabled     = FALSE;

static unsigned int gc_x = 0;
static unsigned int gc_y = 0;

static unsigned int gc_buffer_columns = 0;
static unsigned int gc_buffer_rows    = 0;
static uint8_t *gc_buffer_attributes = NULL;
static uint8_t *gc_buffer_characters = NULL;
static uint8_t *gc_buffer_colorcodes = NULL;

/* VT100 Escape State Machine */
enum vt100state_e {
    ESnormal,
    ESesc,
    ESsquare,
    ESgetpars,
    ESgotpars,
    ESask,
    EScharsize,
    ESsetG0,
    ESsetG1
};

static enum vt100state_e gc_vt100state = ESnormal;

#define MAXPARS 16
static unsigned int gc_par[MAXPARS];
static unsigned int gc_numpars = 0;

/* Character Attributes */
#define ATTR_NONE       0
#define ATTR_BOLD       (1 << 0)
#define ATTR_UNDERLINE  (1 << 1)
#define ATTR_REVERSE    (1 << 2)

static unsigned char gc_attr = ATTR_NONE;
static unsigned char gc_color_code = 0;
static boolean_t gc_wrap_mode = TRUE;
static boolean_t gc_relative_origin = FALSE;
static boolean_t gc_cursor_state = FALSE;
static bool s_raw_mode = false;

static spinlock_t s_vc_lock = SPINLOCK_INIT;

/* Default Colors (RGBA) */
static const uint32_t s_ansi_colors[16] = {
    0x00000000, /* 0: Black */
    0x00AA0000, /* 1: Red */
    0x0000AA00, /* 2: Green */
    0x00AA5500, /* 3: Brown/Yellow */
    0x000000AA, /* 4: Blue */
    0x00AA00AA, /* 5: Magenta */
    0x0000AAAA, /* 6: Cyan */
    0x00AAAAAA, /* 7: Light Gray */
    0x00555555, /* 8: Dark Gray */
    0x00FF5555, /* 9: Bright Red */
    0x0055FF55, /* 10: Bright Green */
    0x00FFFF55, /* 11: Bright Yellow */
    0x005555FF, /* 12: Bright Blue */
    0x00FF55FF, /* 13: Bright Magenta */
    0x0055FFFF, /* 14: Bright Cyan */
    0x00FFFFFF  /* 15: White */
};

static uint32_t gc_fg_color = 0x00FFFFFF;
static uint32_t gc_bg_color = 0x00000000;

/* Forward declarations */
static void gc_putc_normal(unsigned char ch);
static void gc_putc_esc(unsigned char ch);
static void gc_putc_square(unsigned char ch);
static void gc_putc_getpars(unsigned char ch);
static void gc_putc_gotpars(unsigned char ch);
static void gc_putc_askcmd(unsigned char ch);
static void gc_putc_charsizecmd(unsigned char ch);
static void gc_putc_charsetcmd(int g, unsigned char ch);
static void gc_paint_char(unsigned int x, unsigned int y, unsigned char ch,
                          unsigned char attrs, unsigned char color_code);

void vc_reverse_cursor(void) {
    if (!gc_initialized || !vinfo.v_baseaddr) return;
    if (gc_x >= vinfo.v_columns || gc_y >= vinfo.v_rows) return;

    uint32_t *fb = (uint32_t *)vinfo.v_baseaddr;
    uint32_t stride = vinfo.v_rowbytes / 4;
    unsigned int px = gc_x * 8;
    unsigned int py = gc_y * 16;

    for (unsigned int row = 0; row < 16; row++) {
        uint32_t *line = fb + (py + row) * stride + px;
        for (unsigned int col = 0; col < 8; col++) {
            line[col] ^= 0x00FFFFFF;
        }
    }
    gc_cursor_state = !gc_cursor_state;
}

static void vc_clear_line_rect(unsigned int start_col, unsigned int end_col, unsigned int row) {
    if (!vinfo.v_baseaddr) return;
    uint32_t *fb = (uint32_t *)vinfo.v_baseaddr;
    uint32_t stride = vinfo.v_rowbytes / 4;

    unsigned int px_start = start_col * 8;
    unsigned int px_end = end_col * 8;
    if (px_end > vinfo.v_width) px_end = vinfo.v_width;
    unsigned int py = row * 16;

    for (unsigned int r = 0; r < 16 && (py + r) < vinfo.v_height; r++) {
        uint32_t *line = fb + (py + r) * stride;
        for (unsigned int c = px_start; c < px_end; c++) {
            line[c] = gc_bg_color;
        }
    }
}

void vc_clear_screen(void) {
    if (!vinfo.v_baseaddr) return;
    uint32_t *fb = (uint32_t *)vinfo.v_baseaddr;
    uint32_t total_pixels = (vinfo.v_rowbytes / 4) * vinfo.v_height;
    for (uint32_t i = 0; i < total_pixels; i++) {
        fb[i] = gc_bg_color;
    }
    gc_x = 0;
    gc_y = 0;
}

void vc_scroll_up(int num_rows) {
    if (num_rows <= 0 || !vinfo.v_baseaddr) return;
    if ((unsigned int)num_rows >= vinfo.v_rows) {
        vc_clear_screen();
        return;
    }

    uint32_t *fb = (uint32_t *)vinfo.v_baseaddr;
    uint32_t stride = vinfo.v_rowbytes / 4;
    uint32_t pixels_to_scroll = (vinfo.v_rows - num_rows) * 16 * stride;
    uint32_t scroll_offset = num_rows * 16 * stride;

    __builtin_memmove(fb, fb + scroll_offset, pixels_to_scroll * 4);

    /* Clear bottom rows */
    uint32_t bottom_start_row = (vinfo.v_rows - num_rows) * 16;
    for (uint32_t r = bottom_start_row; r < vinfo.v_height; r++) {
        uint32_t *line = fb + r * stride;
        for (uint32_t c = 0; c < vinfo.v_width; c++) {
            line[c] = gc_bg_color;
        }
    }
}

void vc_scroll_down(int num_rows) {
    if (num_rows <= 0 || !vinfo.v_baseaddr) return;
    if ((unsigned int)num_rows >= vinfo.v_rows) {
        vc_clear_screen();
        return;
    }

    uint32_t *fb = (uint32_t *)vinfo.v_baseaddr;
    uint32_t stride = vinfo.v_rowbytes / 4;
    uint32_t pixels_to_scroll = (vinfo.v_rows - num_rows) * 16 * stride;
    uint32_t scroll_offset = num_rows * 16 * stride;

    __builtin_memmove(fb + scroll_offset, fb, pixels_to_scroll * 4);

    /* Clear top rows */
    for (uint32_t r = 0; r < (uint32_t)num_rows * 16; r++) {
        uint32_t *line = fb + r * stride;
        for (uint32_t c = 0; c < vinfo.v_width; c++) {
            line[c] = gc_bg_color;
        }
    }
}

static void gc_paint_char(unsigned int x, unsigned int y, unsigned char ch,
                          unsigned char attrs, unsigned char color_code) {
    (void)color_code;
    if (!vinfo.v_baseaddr || x >= vinfo.v_columns || y >= vinfo.v_rows) return;

    uint32_t *fb = (uint32_t *)vinfo.v_baseaddr;
    uint32_t stride = vinfo.v_rowbytes / 4;
    unsigned int px = x * 8;
    unsigned int py = y * 16;

    const unsigned char *glyph = &iso_font[(unsigned int)ch * 16];

    uint32_t fg = (attrs & ATTR_REVERSE) ? gc_bg_color : gc_fg_color;
    uint32_t bg = (attrs & ATTR_REVERSE) ? gc_fg_color : gc_bg_color;

    if (attrs & ATTR_BOLD) {
        /* brighten */
        fg |= 0x00555555;
    }

    for (unsigned int row = 0; row < 16 && (py + row) < vinfo.v_height; row++) {
        unsigned char bits = (attrs & ATTR_UNDERLINE && row == 14) ? 0xFF : glyph[row];
        uint32_t *line = fb + (py + row) * stride + px;
        for (unsigned int col = 0; col < 8 && (px + col) < vinfo.v_width; col++) {
            line[col] = (bits & (1 << col)) ? fg : bg;
        }
    }
}

static void gc_putc_normal(unsigned char ch) {
    switch (ch) {
    case '\a': /* BEL */
        break;
    case '\b': /* BS */
        if (gc_x > 0) gc_x--;
        break;
    case '\t': /* TAB */
        gc_x = (gc_x + 8) & ~7;
        if (gc_x >= vinfo.v_columns) {
            gc_x = 0;
            if (gc_y + 1 < vinfo.v_rows) gc_y++;
            else vc_scroll_up(1);
        }
        break;
    case '\n': /* LF */
        gc_x = 0;
        if (gc_y + 1 < vinfo.v_rows) {
            gc_y++;
        } else {
            vc_scroll_up(1);
        }
        break;
    case '\r': /* CR */
        gc_x = 0;
        break;
    case '\033': /* ESC */
        gc_vt100state = ESesc;
        break;
    default:
        if (ch >= 32) {
            gc_paint_char(gc_x, gc_y, ch, gc_attr, gc_color_code);
            gc_x++;
            if (gc_x >= vinfo.v_columns) {
                if (gc_wrap_mode) {
                    gc_x = 0;
                    if (gc_y + 1 < vinfo.v_rows) gc_y++;
                    else vc_scroll_up(1);
                } else {
                    gc_x = vinfo.v_columns - 1;
                }
            }
        }
        break;
    }
}

static void gc_putc_esc(unsigned char ch) {
    gc_vt100state = ESnormal;
    switch (ch) {
    case '[':
        gc_vt100state = ESsquare;
        gc_numpars = 0;
        __builtin_memset(gc_par, 0, sizeof(gc_par));
        break;
    case 'c': /* RIS - Reset Initial State */
        gc_attr = ATTR_NONE;
        gc_fg_color = 0x00FFFFFF;
        gc_bg_color = 0x00000000;
        vc_clear_screen();
        break;
    case '7': /* DECSC - Save cursor */
        break;
    case '8': /* DECRC - Restore cursor */
        break;
    case 'D': /* IND - Index */
        if (gc_y + 1 < vinfo.v_rows) gc_y++;
        else vc_scroll_up(1);
        break;
    case 'M': /* RI - Reverse Index */
        if (gc_y > 0) gc_y--;
        else vc_scroll_down(1);
        break;
    case 'E': /* NEL - Next Line */
        gc_x = 0;
        if (gc_y + 1 < vinfo.v_rows) gc_y++;
        else vc_scroll_up(1);
        break;
    case '#':
        gc_vt100state = EScharsize;
        break;
    case '(':
        gc_vt100state = ESsetG0;
        break;
    case ')':
        gc_vt100state = ESsetG1;
        break;
    default:
        break;
    }
}

static void gc_putc_square(unsigned char ch) {
    if (ch >= '0' && ch <= '9') {
        gc_par[gc_numpars] = ch - '0';
        gc_vt100state = ESgetpars;
    } else if (ch == ';') {
        gc_numpars++;
        gc_vt100state = ESgetpars;
    } else if (ch == '?') {
        gc_vt100state = ESask;
    } else {
        gc_putc_gotpars(ch);
    }
}

static void gc_putc_getpars(unsigned char ch) {
    if (ch >= '0' && ch <= '9') {
        gc_par[gc_numpars] = (10 * gc_par[gc_numpars]) + (ch - '0');
    } else if (ch == ';') {
        if (gc_numpars < MAXPARS - 1) gc_numpars++;
    } else {
        gc_putc_gotpars(ch);
    }
}

static void gc_putc_gotpars(unsigned char ch) {
    gc_vt100state = ESnormal;
    unsigned int p1 = (gc_par[0] == 0) ? 1 : gc_par[0];
    unsigned int p2 = (gc_par[1] == 0) ? 1 : gc_par[1];

    switch (ch) {
    case 'A': /* CUU - Cursor Up */
        if (gc_y >= p1) gc_y -= p1;
        else gc_y = 0;
        break;
    case 'B': /* CUD - Cursor Down */
        gc_y += p1;
        if (gc_y >= vinfo.v_rows) gc_y = vinfo.v_rows - 1;
        break;
    case 'C': /* CUF - Cursor Forward */
        gc_x += p1;
        if (gc_x >= vinfo.v_columns) gc_x = vinfo.v_columns - 1;
        break;
    case 'D': /* CUB - Cursor Back */
        if (gc_x >= p1) gc_x -= p1;
        else gc_x = 0;
        break;
    case 'H': /* CUP - Cursor Position */
    case 'f':
        gc_y = (p1 > 0) ? (p1 - 1) : 0;
        gc_x = (p2 > 0) ? (p2 - 1) : 0;
        if (gc_y >= vinfo.v_rows) gc_y = vinfo.v_rows - 1;
        if (gc_x >= vinfo.v_columns) gc_x = vinfo.v_columns - 1;
        break;
    case 'J': /* ED - Erase in Display */
        if (gc_par[0] == 0) {
            /* clear cursor to end of screen */
            vc_clear_line_rect(gc_x, vinfo.v_columns, gc_y);
            for (unsigned int r = gc_y + 1; r < vinfo.v_rows; r++) {
                vc_clear_line_rect(0, vinfo.v_columns, r);
            }
        } else if (gc_par[0] == 1) {
            /* clear beginning of screen to cursor */
            for (unsigned int r = 0; r < gc_y; r++) {
                vc_clear_line_rect(0, vinfo.v_columns, r);
            }
            vc_clear_line_rect(0, gc_x + 1, gc_y);
        } else if (gc_par[0] == 2) {
            /* clear whole display */
            vc_clear_screen();
        }
        break;
    case 'K': /* EL - Erase in Line */
        if (gc_par[0] == 0) {
            vc_clear_line_rect(gc_x, vinfo.v_columns, gc_y);
        } else if (gc_par[0] == 1) {
            vc_clear_line_rect(0, gc_x + 1, gc_y);
        } else if (gc_par[0] == 2) {
            vc_clear_line_rect(0, vinfo.v_columns, gc_y);
        }
        break;
    case 'm': /* SGR - Select Graphic Rendition */
        for (unsigned int i = 0; i <= gc_numpars; i++) {
            unsigned int p = gc_par[i];
            if (p == 0) {
                gc_attr = ATTR_NONE;
                gc_fg_color = 0x00FFFFFF;
                gc_bg_color = 0x00000000;
            } else if (p == 1) {
                gc_attr |= ATTR_BOLD;
            } else if (p == 4) {
                gc_attr |= ATTR_UNDERLINE;
            } else if (p == 7) {
                gc_attr |= ATTR_REVERSE;
            } else if (p == 22) {
                gc_attr &= ~ATTR_BOLD;
            } else if (p == 24) {
                gc_attr &= ~ATTR_UNDERLINE;
            } else if (p == 27) {
                gc_attr &= ~ATTR_REVERSE;
            } else if (p >= 30 && p <= 37) {
                gc_fg_color = s_ansi_colors[p - 30];
            } else if (p == 39) {
                gc_fg_color = 0x00FFFFFF;
            } else if (p >= 40 && p <= 47) {
                gc_bg_color = s_ansi_colors[p - 40];
            } else if (p == 49) {
                gc_bg_color = 0x00000000;
            } else if (p >= 90 && p <= 97) {
                gc_fg_color = s_ansi_colors[p - 90 + 8];
            } else if (p >= 100 && p <= 107) {
                gc_bg_color = s_ansi_colors[p - 100 + 8];
            }
        }
        break;
    default:
        break;
    }
}

static void gc_putc_askcmd(unsigned char ch) {
    if (ch >= '0' && ch <= '9') {
        gc_par[gc_numpars] = (10 * gc_par[gc_numpars]) + (ch - '0');
        return;
    }
    gc_vt100state = ESnormal;
    switch (gc_par[0]) {
    case 6:
        gc_relative_origin = (ch == 'h');
        break;
    case 7:
        gc_wrap_mode = (ch == 'h');
        break;
    case 25:
        /* cursor visibility */
        break;
    default:
        break;
    }
}

static void gc_putc_charsizecmd(unsigned char ch) {
    (void)ch;
    gc_vt100state = ESnormal;
}

static void gc_putc_charsetcmd(int g, unsigned char ch) {
    (void)g; (void)ch;
    gc_vt100state = ESnormal;
}

static void gc_putc_char(unsigned char ch) {
    switch (gc_vt100state) {
    case ESnormal:
        gc_putc_normal(ch);
        break;
    case ESesc:
        gc_putc_esc(ch);
        break;
    case ESsquare:
        gc_putc_square(ch);
        break;
    case ESgetpars:
        gc_putc_getpars(ch);
        break;
    case ESgotpars:
        gc_putc_gotpars(ch);
        break;
    case ESask:
        gc_putc_askcmd(ch);
        break;
    case EScharsize:
        gc_vt100state = ESnormal;
        break;
    case ESsetG0:
        gc_putc_charsetcmd(0, ch);
        break;
    case ESsetG1:
        gc_putc_charsetcmd(1, ch);
        break;
    default:
        gc_vt100state = ESnormal;
        gc_putc_normal(ch);
        break;
    }
}

void vc_putchar(char ch) {
    if (!gc_initialized || !gc_enabled) return;
    irq_flags_t flags = spinlock_lock_irqsave(&s_vc_lock);
    gc_putc_char((unsigned char)ch);
    spinlock_unlock_irqrestore(&s_vc_lock, flags);
}

void vc_puts(const char *s) {
    if (!s || !gc_initialized || !gc_enabled) return;
    irq_flags_t flags = spinlock_lock_irqsave(&s_vc_lock);
    while (*s) {
        gc_putc_char((unsigned char)*s++);
    }
    spinlock_unlock_irqrestore(&s_vc_lock, flags);
}

void vc_putc_raw(char c) {
    vc_putchar(c);
}

void vc_set_raw_mode(bool raw) {
    s_raw_mode = raw;
}

bool vc_get_raw_mode(void) {
    return s_raw_mode;
}

struct vc_info *vc_get_info(void) {
    return &vinfo;
}

void video_console_init(unsigned long baseaddr, uint64_t physaddr,
                        unsigned int width, unsigned int height,
                        unsigned int depth, unsigned int pitch) {
    irq_flags_t flags = spinlock_lock_irqsave(&s_vc_lock);

    vinfo.v_baseaddr = baseaddr;
    vinfo.v_physaddr = physaddr;
    vinfo.v_width = width;
    vinfo.v_height = height;
    vinfo.v_depth = depth;
    vinfo.v_rowbytes = pitch;
    vinfo.v_columns = width / 8;
    vinfo.v_rows = height / 16;
    vinfo.v_scale = 1;
    vinfo.v_rotate = 0;

    gc_buffer_columns = vinfo.v_columns;
    gc_buffer_rows = vinfo.v_rows;
    gc_x = 0;
    gc_y = 0;
    gc_attr = ATTR_NONE;
    gc_fg_color = 0x00FFFFFF;
    gc_bg_color = 0x00000000;
    gc_vt100state = ESnormal;
    gc_wrap_mode = TRUE;

    gc_initialized = TRUE;
    gc_enabled = TRUE;

    vc_clear_screen();

    spinlock_unlock_irqrestore(&s_vc_lock, flags);
}
