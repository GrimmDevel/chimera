// console and vga font engine
#include <kernel/xiu_types.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/proc.h>
#include <stdarg.h>

// basic COM1 Serial Output
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

void console_init(void) {
    // test if COM1 is present by writing and reading scratch register
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
    if (!s_serial_present) return;
    int timeout = 50000;
    while (!(inb(COM1_PORT + 5) & 0x20) && --timeout > 0);
    if (timeout > 0) {
        outb(COM1_PORT, c);
    }
}

void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

// 8x16 vga font table
static const u8 s_vga_font[256][16] = {
[0x20]={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
[0x21]={0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
[0x22]={0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
[0x23]={0x00,0x36,0x36,0x7F,0x36,0x36,0x36,0x7F,0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},
[0x24]={0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00,0x00},
[0x25]={0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00,0x00,0x00},
[0x26]={0x00,0x38,0x6C,0x38,0x60,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00,0x00},
[0x27]={0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
[0x28]={0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00},
[0x29]={0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x00},
[0x2A]={0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
[0x2B]={0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
[0x2C]={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00},
[0x2D]={0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
[0x2E]={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
[0x2F]={0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00,0x00,0x00},
[0x30]={0x00,0x7C,0xC6,0xC6,0xCE,0xD6,0xD6,0xE6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
[0x31]={0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00},
[0x32]={0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00},
[0x33]={0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
[0x34]={0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00,0x00},
[0x35]={0x00,0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
[0x36]={0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
[0x37]={0x00,0xFE,0xC6,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00,0x00},
[0x38]={0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
[0x39]={0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00,0x00},
[0x3A]={0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
[0x3B]={0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
[0x3C]={0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00,0x00},
[0x3D]={0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
[0x3E]={0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00,0x00},
[0x3F]={0x00,0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
[0x40]={0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0xC0,0x7C,0x00,0x00,0x00,0x00,0x00},
[0x41]={0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00},
[0x42]={0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00,0x00},
[0x43]={0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
[0x44]={0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00,0x00},
[0x45]={0x00,0xFE,0x62,0x60,0x64,0x7C,0x64,0x60,0x60,0x62,0xFE,0x00,0x00,0x00,0x00,0x00},
[0x46]={0x00,0xFE,0x66,0x62,0x64,0x7C,0x64,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00},
[0x47]={0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00,0x00},
[0x48]={0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00},
[0x49]={0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00},
[0x4A]={0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00,0x00},
[0x4B]={0x00,0xE6,0x66,0x6C,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,0x00},
[0x4C]={0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00,0x00},
[0x4D]={0x00,0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00},
[0x4E]={0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00},
[0x4F]={0x00,0x38,0x6C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00,0x00,0x00,0x00,0x00},
[0x50]={0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00},
[0x51]={0x00,0x78,0xCC,0xCC,0xCC,0xCC,0xCC,0xDC,0x78,0x1C,0x00,0x00,0x00,0x00,0x00,0x00},
[0x52]={0x00,0xFC,0x66,0x66,0x66,0x7C,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,0x00},
[0x53]={0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
[0x54]={0x00,0x7E,0x7E,0x5A,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00},
[0x55]={0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
[0x56]={0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00,0x00},
[0x57]={0x00,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xFE,0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00},
[0x58]={0x00,0xC6,0xC6,0x6C,0x6C,0x38,0x38,0x6C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00},
[0x59]={0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00},
[0x5A]={0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00},
[0x5B]={0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00,0x00},
[0x5C]={0x00,0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00},
[0x5D]={0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00,0x00},
[0x5E]={0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
[0x5F]={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00},
[0x60]={0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
[0x61]={0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00},
[0x62]={0x00,0xE0,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0xDC,0x00,0x00,0x00,0x00,0x00},
[0x63]={0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
[0x64]={0x00,0x1C,0x0C,0x0C,0x7C,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00},
[0x65]={0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
[0x66]={0x00,0x1C,0x36,0x32,0x30,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00,0x00},
[0x67]={0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00,0x00,0x00},
[0x68]={0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,0x00},
[0x69]={0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00},
[0x6A]={0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00,0x00,0x00},
[0x6B]={0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00,0x00},
[0x6C]={0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00},
[0x6D]={0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xD6,0x00,0x00,0x00,0x00,0x00},
[0x6E]={0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
[0x6F]={0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
[0x70]={0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00,0x00,0x00},
[0x71]={0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00,0x00,0x00},
[0x72]={0x00,0x00,0x00,0x00,0xDC,0x76,0x62,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00},
[0x73]={0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
[0x74]={0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00,0x00},
[0x75]={0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00},
[0x76]={0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00,0x00},
[0x77]={0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00},
[0x78]={0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00},
[0x79]={0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00,0x00,0x00},
[0x7A]={0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00},
[0x7B]={0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00,0x00},
[0x7C]={0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
[0x7D]={0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00,0x00},
[0x7E]={0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

// ansi 16-color palette
static const u32 s_ansi_colors[16] = {
    0xFF000000, // 0: Black
    0xFFCC0000, // 1: Red
    0xFF4E9A06, // 2: Green
    0xFFC4A000, // 3: Yellow
    0xFF3465A4, // 4: Blue
    0xFF75507B, // 5: Magenta
    0xFF06989A, // 6: Cyan
    0xFFD3D7CF, // 7: Light Gray
    0xFF555753, // 8: Dark Gray
    0xFFEF2929, // 9: Bright Red
    0xFF8AE234, // 10: Bright Green
    0xFFFCE94F, // 11: Bright Yellow
    0xFF729FCF, // 12: Bright Blue
    0xFFAD7FA8, // 13: Bright Magenta
    0xFF34E2E2, // 14: Bright Cyan
    0xFFEEEEEC, // 15: Bright White
};

#define DEFAULT_FG_COLOR  0xFFAAAAAA /* Classic VGA Light Gray */
#define DEFAULT_BG_COLOR  0xFF000000 /* Pure Black Terminal */

// framebuffer console state
typedef struct fb_console {
    u32  *fb;
    u32   width;
    u32   height;
    u32   pitch_px;
    u32   cols;
    u32   rows;
    u32   cur_col;
    u32   cur_row;
    u32   fg_color;
    u32   bg_color;
    bool  bold;
    bool  active;
    spinlock_t lock;

    // ansi escape parser state
    u8    esc_state;
    char  esc_buf[32];
    u8    esc_len;
} fb_console_t;

static fb_console_t g_fb_con;

#define CONSOLE_MAX_COLS 160
#define CONSOLE_MAX_ROWS 60
#define CONSOLE_SCROLLBACK_LINES 1024

typedef struct {
    u8  ch;
    u32 fg;
    u32 bg;
} console_cell_t;

static console_cell_t s_scrollback[CONSOLE_SCROLLBACK_LINES][CONSOLE_MAX_COLS];
static u32 s_scrollback_head = 0;
static u32 s_scrollback_total = 0;
static u32 s_scroll_offset = 0;

#define CONSOLE_SHADOW_MAX_PX (1920 * 1080)
static u32 s_fb_shadow[CONSOLE_SHADOW_MAX_PX];

void console_fb_init(u32 *fb_ptr, u32 width, u32 height, u32 pitch) {
    if (!fb_ptr || width < 8 || height < 16) return;

    spinlock_init(&g_fb_con.lock);
    g_fb_con.fb = fb_ptr;
    g_fb_con.width = width;
    g_fb_con.height = height;
    g_fb_con.pitch_px = pitch;
    g_fb_con.cols = width / 8;
    if (g_fb_con.cols > CONSOLE_MAX_COLS) g_fb_con.cols = CONSOLE_MAX_COLS;
    g_fb_con.rows = height / 16;
    if (g_fb_con.rows > CONSOLE_MAX_ROWS) g_fb_con.rows = CONSOLE_MAX_ROWS;
    g_fb_con.cur_col = 0;
    g_fb_con.cur_row = 0;
    g_fb_con.fg_color = DEFAULT_FG_COLOR;
    g_fb_con.bg_color = DEFAULT_BG_COLOR;
    g_fb_con.bold = false;
    g_fb_con.esc_state = 0;
    g_fb_con.esc_len = 0;
    g_fb_con.active = true;

    s_scrollback_head = 0;
    s_scrollback_total = 0;
    s_scroll_offset = 0;

    for (u32 r = 0; r < CONSOLE_SCROLLBACK_LINES; r++) {
        for (u32 c = 0; c < CONSOLE_MAX_COLS; c++) {
            s_scrollback[r][c].ch = ' ';
            s_scrollback[r][c].fg = DEFAULT_FG_COLOR;
            s_scrollback[r][c].bg = DEFAULT_BG_COLOR;
        }
    }

    // clear screen and shadow buffer
    u32 total_px = pitch * height;
    if (total_px > CONSOLE_SHADOW_MAX_PX) total_px = CONSOLE_SHADOW_MAX_PX;
    for (u32 i = 0; i < total_px; i++) {
        s_fb_shadow[i] = DEFAULT_BG_COLOR;
        g_fb_con.fb[i] = DEFAULT_BG_COLOR;
    }
}

static console_cell_t s_screen_cache[CONSOLE_MAX_ROWS][CONSOLE_MAX_COLS];

static void fb_raw_draw_cell(u32 col, u32 row, u8 ch, u32 fg, u32 bg) {
    if (!g_fb_con.active || col >= g_fb_con.cols || row >= g_fb_con.rows) return;

    if (col < CONSOLE_MAX_COLS && row < CONSOLE_MAX_ROWS) {
        s_screen_cache[row][col].ch = ch;
        s_screen_cache[row][col].fg = fg;
        s_screen_cache[row][col].bg = bg;
    }

    const u8 *glyph = s_vga_font[ch];
    u32 px_x = col * 8;
    u32 px_y = row * 16;

    for (int r = 0; r < 16; r++) {
        u8 bits = glyph[r];
        u32 offset = (px_y + r) * g_fb_con.pitch_px + px_x;
        if (offset + 8 > CONSOLE_SHADOW_MAX_PX) continue;

        u32 *shadow_line = s_fb_shadow + offset;
        u32 *fb_line = g_fb_con.fb + offset;

        shadow_line[0] = (bits & 0x80) ? fg : bg;
        shadow_line[1] = (bits & 0x40) ? fg : bg;
        shadow_line[2] = (bits & 0x20) ? fg : bg;
        shadow_line[3] = (bits & 0x10) ? fg : bg;
        shadow_line[4] = (bits & 0x08) ? fg : bg;
        shadow_line[5] = (bits & 0x04) ? fg : bg;
        shadow_line[6] = (bits & 0x02) ? fg : bg;
        shadow_line[7] = (bits & 0x01) ? fg : bg;

        // write-only to VRAM
        fb_line[0] = shadow_line[0];
        fb_line[1] = shadow_line[1];
        fb_line[2] = shadow_line[2];
        fb_line[3] = shadow_line[3];
        fb_line[4] = shadow_line[4];
        fb_line[5] = shadow_line[5];
        fb_line[6] = shadow_line[6];
        fb_line[7] = shadow_line[7];
    }
}

static void fb_draw_char_cell(u32 col, u32 row, u8 ch, u32 fg, u32 bg) {
    if (!g_fb_con.active || col >= g_fb_con.cols || row >= g_fb_con.rows) return;
    if (col >= CONSOLE_MAX_COLS) return;

    u32 buf_row = (s_scrollback_head + row) % CONSOLE_SCROLLBACK_LINES;
    s_scrollback[buf_row][col].ch = ch;
    s_scrollback[buf_row][col].fg = fg;
    s_scrollback[buf_row][col].bg = bg;

    if (s_scroll_offset == 0) {
        fb_raw_draw_cell(col, row, ch, fg, bg);
    }
}

static void console_refresh_view_locked(void) {
    if (!g_fb_con.active) return;

    for (u32 r = 0; r < g_fb_con.rows; r++) {
        i32 line_idx = (i32)s_scrollback_head - (i32)s_scroll_offset + (i32)r;
        while (line_idx < 0) line_idx += CONSOLE_SCROLLBACK_LINES;
        u32 buf_row = (u32)line_idx % CONSOLE_SCROLLBACK_LINES;

        for (u32 c = 0; c < g_fb_con.cols && c < CONSOLE_MAX_COLS; c++) {
            console_cell_t cell = s_scrollback[buf_row][c];
            u8 ch = cell.ch ? cell.ch : ' ';
            u32 fg = cell.fg ? cell.fg : DEFAULT_FG_COLOR;
            u32 bg = cell.bg;

            fb_raw_draw_cell(c, r, ch, fg, bg);
        }
    }
}

void console_scroll_viewport(int delta) {
    if (!g_fb_con.active) return;
    irq_flags_t irq = spinlock_lock_irqsave(&g_fb_con.lock);

    u32 max_scroll = s_scrollback_total;
    if (max_scroll > CONSOLE_SCROLLBACK_LINES - g_fb_con.rows) {
        max_scroll = CONSOLE_SCROLLBACK_LINES - g_fb_con.rows;
    }

    if (max_scroll == 0) {
        spinlock_unlock_irqrestore(&g_fb_con.lock, irq);
        return;
    }

    // hard stop at boundaries
    if ((s_scroll_offset == max_scroll && delta > 0) ||
        (s_scroll_offset == 0 && delta < 0)) {
        spinlock_unlock_irqrestore(&g_fb_con.lock, irq);
        return;
    }

    i32 new_offset = (i32)s_scroll_offset + delta;
    if (new_offset > (i32)max_scroll) new_offset = (i32)max_scroll;
    if (new_offset < 0) new_offset = 0;

    if ((u32)new_offset != s_scroll_offset) {
        s_scroll_offset = (u32)new_offset;
        console_refresh_view_locked();
    }
    spinlock_unlock_irqrestore(&g_fb_con.lock, irq);
}

void console_scroll_to_bottom(void) {
    if (!g_fb_con.active) return;
    irq_flags_t irq = spinlock_lock_irqsave(&g_fb_con.lock);
    if (s_scroll_offset != 0) {
        s_scroll_offset = 0;
        console_refresh_view_locked();
    }
    spinlock_unlock_irqrestore(&g_fb_con.lock, irq);
}

static void fb_scroll_up(void) {
    if (!g_fb_con.active || g_fb_con.rows == 0) return;

    s_scrollback_head = (s_scrollback_head + 1) % CONSOLE_SCROLLBACK_LINES;
    s_scrollback_total++;

    // clear bottom row in history ring
    u32 bottom_buf_row = (s_scrollback_head + g_fb_con.rows - 1) % CONSOLE_SCROLLBACK_LINES;
    for (u32 c = 0; c < CONSOLE_MAX_COLS; c++) {
        s_scrollback[bottom_buf_row][c].ch = ' ';
        s_scrollback[bottom_buf_row][c].fg = DEFAULT_FG_COLOR;
        s_scrollback[bottom_buf_row][c].bg = g_fb_con.bg_color;
    }

    if (s_scroll_offset == 0) {
        u32 copy_scanlines = (g_fb_con.rows - 1) * 16;
        u32 row_step_px = 16 * g_fb_con.pitch_px;
        u32 total_words = copy_scanlines * g_fb_con.pitch_px;

        // 1. Fast RAM-to-RAM scanline shift in CPU cache
        u64 *shadow_dst = (u64 *)s_fb_shadow;
        u64 *shadow_src = (u64 *)(s_fb_shadow + row_step_px);
        u32 u64_count = total_words / 2;

        for (u32 i = 0; i < u64_count; i++) {
            shadow_dst[i] = shadow_src[i];
        }

        // clear bottom row in shadow
        u32 *shadow_bottom = s_fb_shadow + copy_scanlines * g_fb_con.pitch_px;
        u32 clear_words = 16 * g_fb_con.pitch_px;
        for (u32 i = 0; i < clear_words; i++) {
            shadow_bottom[i] = g_fb_con.bg_color;
        }

        // 2. Fast Write-Only copy from RAM Shadow to VRAM
        u64 *fb_dst = (u64 *)g_fb_con.fb;
        u32 total_u64 = (g_fb_con.rows * 16 * g_fb_con.pitch_px) / 2;
        for (u32 i = 0; i < total_u64; i++) {
            fb_dst[i] = shadow_dst[i];
        }
    } else {
        console_refresh_view_locked();
    }
}

static void fb_clear_screen(void) {
    if (!g_fb_con.active) return;
    u32 total_px = g_fb_con.pitch_px * g_fb_con.height;
    if (total_px > CONSOLE_SHADOW_MAX_PX) total_px = CONSOLE_SHADOW_MAX_PX;

    for (u32 i = 0; i < total_px; i++) {
        s_fb_shadow[i] = g_fb_con.bg_color;
        g_fb_con.fb[i] = g_fb_con.bg_color;
    }
    for (u32 r = 0; r < g_fb_con.rows; r++) {
        u32 buf_row = (s_scrollback_head + r) % CONSOLE_SCROLLBACK_LINES;
        for (u32 c = 0; c < CONSOLE_MAX_COLS; c++) {
            s_scrollback[buf_row][c].ch = ' ';
            s_scrollback[buf_row][c].fg = DEFAULT_FG_COLOR;
            s_scrollback[buf_row][c].bg = g_fb_con.bg_color;
        }
    }
    s_scroll_offset = 0;
    g_fb_con.cur_col = 0;
    g_fb_con.cur_row = 0;
}

static void fb_clear_to_eol(void) {
    if (!g_fb_con.active) return;
    for (u32 c = g_fb_con.cur_col; c < g_fb_con.cols; c++) {
        fb_draw_char_cell(c, g_fb_con.cur_row, ' ', g_fb_con.fg_color, g_fb_con.bg_color);
    }
}

// parse simple integer from string pointer
static int parse_int(const char **p) {
    int val = 0;
    while (**p >= '0' && **p <= '9') {
        val = val * 10 + (**p - '0');
        (*p)++;
    }
    return val;
}

static void fb_handle_csi(char cmd, const char *params) {
    const char *p = params;
    int arg1 = 0, arg2 = 0;

    if (*p) {
        arg1 = parse_int(&p);
        if (*p == ';') {
            p++;
            arg2 = parse_int(&p);
        }
    }

    switch (cmd) {
        case 'm': { // sgr Color / Attribute
            const char *sp = params;
            if (!*sp) {
                g_fb_con.fg_color = DEFAULT_FG_COLOR;
                g_fb_con.bg_color = DEFAULT_BG_COLOR;
                g_fb_con.bold = false;
                break;
            }
            while (*sp) {
                int code = parse_int(&sp);
                if (code == 0) {
                    g_fb_con.fg_color = DEFAULT_FG_COLOR;
                    g_fb_con.bg_color = DEFAULT_BG_COLOR;
                    g_fb_con.bold = false;
                } else if (code == 1) {
                    g_fb_con.bold = true;
                } else if (code >= 30 && code <= 37) {
                    int idx = (code - 30) + (g_fb_con.bold ? 8 : 0);
                    g_fb_con.fg_color = s_ansi_colors[idx];
                } else if (code == 39) {
                    g_fb_con.fg_color = DEFAULT_FG_COLOR;
                } else if (code >= 40 && code <= 47) {
                    g_fb_con.bg_color = s_ansi_colors[code - 40];
                } else if (code == 49) {
                    g_fb_con.bg_color = DEFAULT_BG_COLOR;
                } else if (code >= 90 && code <= 97) {
                    g_fb_con.fg_color = s_ansi_colors[code - 90 + 8];
                } else if (code >= 100 && code <= 107) {
                    g_fb_con.bg_color = s_ansi_colors[code - 100 + 8];
                }
                if (*sp == ';') sp++;
            }
            break;
        }
        case 'H':
        case 'f': { // cursor position
            int row = arg1 > 0 ? arg1 - 1 : 0;
            int col = arg2 > 0 ? arg2 - 1 : 0;
            if (row < (int)g_fb_con.rows) g_fb_con.cur_row = row;
            if (col < (int)g_fb_con.cols) g_fb_con.cur_col = col;
            break;
        }
        case 'J': { // clear display
            if (arg1 == 2) fb_clear_screen();
            break;
        }
        case 'K': { // clear in line
            fb_clear_to_eol();
            break;
        }
        case 'C': { // cursor forward
            int n = arg1 > 0 ? arg1 : 1;
            g_fb_con.cur_col += n;
            if (g_fb_con.cur_col >= g_fb_con.cols) g_fb_con.cur_col = g_fb_con.cols - 1;
            break;
        }
        case 'D': { // cursor back
            int n = arg1 > 0 ? arg1 : 1;
            if (g_fb_con.cur_col >= (u32)n) g_fb_con.cur_col -= n;
            else g_fb_con.cur_col = 0;
            break;
        }
        default:
            break;
    }
}

void console_putc(char c) {
    // always mirror to serial
    if (c == '\n') serial_putc('\r');
    serial_putc(c);

    if (!g_fb_con.active) return;

    irq_flags_t irq = spinlock_lock_irqsave(&g_fb_con.lock);

    // ansi Escape Parser
    if (g_fb_con.esc_state == 1) {
        if (c == '[') {
            g_fb_con.esc_state = 2;
            g_fb_con.esc_len = 0;
            g_fb_con.esc_buf[0] = '\0';
            spinlock_unlock_irqrestore(&g_fb_con.lock, irq);
            return;
        }
        g_fb_con.esc_state = 0;
    } else if (g_fb_con.esc_state == 2) {
        if ((c >= '0' && c <= '9') || c == ';') {
            if (g_fb_con.esc_len < sizeof(g_fb_con.esc_buf) - 1) {
                g_fb_con.esc_buf[g_fb_con.esc_len++] = c;
                g_fb_con.esc_buf[g_fb_con.esc_len] = '\0';
            }
            spinlock_unlock_irqrestore(&g_fb_con.lock, irq);
            return;
        }
        // command byte terminated the CSI sequence
        fb_handle_csi(c, g_fb_con.esc_buf);
        g_fb_con.esc_state = 0;
        spinlock_unlock_irqrestore(&g_fb_con.lock, irq);
        return;
    }

    if (c == 0x1B) { // esc
        g_fb_con.esc_state = 1;
        spinlock_unlock_irqrestore(&g_fb_con.lock, irq);
        return;
    }

    // standard character processing
    if (c == '\n') {
        g_fb_con.cur_col = 0;
        g_fb_con.cur_row++;
        if (g_fb_con.cur_row >= g_fb_con.rows) {
            fb_scroll_up();
            g_fb_con.cur_row = g_fb_con.rows - 1;
        }
    } else if (c == '\r') {
        g_fb_con.cur_col = 0;
    } else if (c == '\b') {
        if (g_fb_con.cur_col > 0) {
            g_fb_con.cur_col--;
            fb_draw_char_cell(g_fb_con.cur_col, g_fb_con.cur_row, ' ',
                              g_fb_con.fg_color, g_fb_con.bg_color);
        }
    } else if (c == '\t') {
        g_fb_con.cur_col = (g_fb_con.cur_col + 8) & ~7;
        if (g_fb_con.cur_col >= g_fb_con.cols) {
            g_fb_con.cur_col = 0;
            g_fb_con.cur_row++;
            if (g_fb_con.cur_row >= g_fb_con.rows) {
                fb_scroll_up();
                g_fb_con.cur_row = g_fb_con.rows - 1;
            }
        }
    } else if ((u8)c >= 0x20) {
        fb_draw_char_cell(g_fb_con.cur_col, g_fb_con.cur_row, (u8)c,
                          g_fb_con.fg_color, g_fb_con.bg_color);
        g_fb_con.cur_col++;
        if (g_fb_con.cur_col >= g_fb_con.cols) {
            g_fb_con.cur_col = 0;
            g_fb_con.cur_row++;
            if (g_fb_con.cur_row >= g_fb_con.rows) {
                fb_scroll_up();
                g_fb_con.cur_row = g_fb_con.rows - 1;
            }
        }
    }

    spinlock_unlock_irqrestore(&g_fb_con.lock, irq);
}

void console_write(const char *buf, usize len) {
    for (usize i = 0; i < len; i++) {
        console_putc(buf[i]);
    }
}

#define CONSOLE_IN_BUF_SIZE 1024
#define CONSOLE_RAW_BUF_SIZE 1024

static volatile char       s_line_buf[CONSOLE_IN_BUF_SIZE];
static volatile usize      s_line_len = 0;
static volatile bool       s_line_ready = false;

static volatile char       s_raw_buf[CONSOLE_RAW_BUF_SIZE];
static volatile usize      s_raw_head = 0;
static volatile usize      s_raw_tail = 0;
static volatile usize      s_raw_count = 0;
static volatile bool       s_raw_mode = false;

static spinlock_t s_in_lock = SPINLOCK_INIT;

static void console_write_str(const char *s) {
    while (*s) {
        console_putc(*s++);
    }
}

static int s_esc_state = 0;

#define CONSOLE_HISTORY_MAX 32
static char   s_history[CONSOLE_HISTORY_MAX][CONSOLE_IN_BUF_SIZE];
static u32    s_history_count = 0;
static i32    s_history_idx = -1;
static char   s_saved_line[CONSOLE_IN_BUF_SIZE];
static usize  s_saved_len = 0;

static void console_erase_input(void) {
    while (s_line_len > 0) {
        s_line_len--;
        console_putc('\b');
    }
}

// feed character from PS/2 Keyboard IRQ or Serial RX
void console_in_push(char c) {
    char to_echo = 0;
    irq_flags_t irq = spinlock_lock_irqsave(&s_in_lock);

    // raw mode bypass
    if (s_raw_mode) {
        if (s_raw_count < CONSOLE_RAW_BUF_SIZE) {
            s_raw_buf[s_raw_tail] = c;
            s_raw_tail = (s_raw_tail + 1) % CONSOLE_RAW_BUF_SIZE;
            s_raw_count++;
        }
        spinlock_unlock_irqrestore(&s_in_lock, irq);
        return;
    }

    if (s_esc_state == 0) {
        if (c == '\033') {
            s_esc_state = 1;
            spinlock_unlock_irqrestore(&s_in_lock, irq);
            return;
        }
    } else if (s_esc_state == 1) {
        if (c == '[') {
            s_esc_state = 2;
        } else {
            s_esc_state = 0;
        }
        spinlock_unlock_irqrestore(&s_in_lock, irq);
        return;
    } else if (s_esc_state == 2) {
        s_esc_state = 0;
        if (c == 'A') { // UP arrow: history back
            if (s_history_count > 0) {
                if (s_history_idx == -1) {
                    // save current input before browsing
                    s_saved_len = s_line_len;
                    for (usize i = 0; i < s_line_len; i++) {
                        s_saved_line[i] = s_line_buf[i];
                    }
                    s_saved_line[s_line_len] = '\0';
                    s_history_idx = (i32)s_history_count - 1;
                } else if (s_history_idx > 0 && ((i32)s_history_count - s_history_idx) < (i32)CONSOLE_HISTORY_MAX) {
                    s_history_idx--;
                }

                console_erase_input();
                const char *hist = s_history[s_history_idx % CONSOLE_HISTORY_MAX];
                usize hlen = __builtin_strlen(hist);
                for (usize i = 0; i < hlen && i < CONSOLE_IN_BUF_SIZE - 2; i++) {
                    s_line_buf[i] = hist[i];
                    console_putc(hist[i]);
                }
                s_line_len = (hlen < CONSOLE_IN_BUF_SIZE - 2) ? hlen : (CONSOLE_IN_BUF_SIZE - 2);
            }
            spinlock_unlock_irqrestore(&s_in_lock, irq);
            return;
        } else if (c == 'B') { // DOWN arrow: history forward
            if (s_history_count > 0 && s_history_idx != -1) {
                if (s_history_idx < (i32)s_history_count - 1) {
                    s_history_idx++;
                    console_erase_input();
                    const char *hist = s_history[s_history_idx % CONSOLE_HISTORY_MAX];
                    usize hlen = __builtin_strlen(hist);
                    for (usize i = 0; i < hlen && i < CONSOLE_IN_BUF_SIZE - 2; i++) {
                        s_line_buf[i] = hist[i];
                        console_putc(hist[i]);
                    }
                    s_line_len = (hlen < CONSOLE_IN_BUF_SIZE - 2) ? hlen : (CONSOLE_IN_BUF_SIZE - 2);
                } else {
                    // restore saved uncommitted input
                    s_history_idx = -1;
                    console_erase_input();
                    for (usize i = 0; i < s_saved_len && i < CONSOLE_IN_BUF_SIZE - 2; i++) {
                        s_line_buf[i] = s_saved_line[i];
                        console_putc(s_saved_line[i]);
                    }
                    s_line_len = s_saved_len;
                }
            }
            spinlock_unlock_irqrestore(&s_in_lock, irq);
            return;
        }
        spinlock_unlock_irqrestore(&s_in_lock, irq);
        return;
    } else {
        s_esc_state = 0;
    }

    if (c == 0x03) { // ctrl+C
        s_line_len = 0;
        s_line_buf[0] = '\n';
        s_line_len = 1;
        s_line_ready = true;
        s_history_idx = -1;
        spinlock_unlock_irqrestore(&s_in_lock, irq);

        console_write_str("^C\n");
        extern xiu_task_t *current_task(void);
        extern xiu_error_t proc_signal(xiu_proc_t *proc, int sig);
        xiu_task_t *task = current_task();
        if (task && task->ta_proc && task->ta_proc->p_pid > 1) {
            proc_signal(task->ta_proc, 2);
        }
        return;
    } else if (c == 0x04) { // ctrl+D
        s_line_ready = true;
    } else if (c == 0x0C) { // ctrl+L
        spinlock_unlock_irqrestore(&s_in_lock, irq);
        console_write_str("\033[2J\033[H");
        return;
    } else if (c == 0x15) { // ctrl+U
        console_erase_input();
    } else if (c == 0x17) { // ctrl+W
        while (s_line_len > 0 && s_line_buf[s_line_len - 1] == ' ') {
            s_line_len--;
            console_putc('\b');
        }
        while (s_line_len > 0 && s_line_buf[s_line_len - 1] != ' ') {
            s_line_len--;
            console_putc('\b');
        }
    } else if (c == 0x08 || c == 0x7F) { // backspace
        if (s_line_len > 0) {
            s_line_len--;
            to_echo = '\b';
        }
    } else if (c == '\n' || c == '\r') {
        // save completed command into history
        if (s_line_len > 0) {
            bool dup = false;
            if (s_history_count > 0) {
                u32 last_slot = (s_history_count - 1) % CONSOLE_HISTORY_MAX;
                if (__builtin_strlen(s_history[last_slot]) == s_line_len &&
                    __builtin_strncmp(s_history[last_slot], (const char *)s_line_buf, s_line_len) == 0) {
                    dup = true;
                }
            }
            if (!dup) {
                u32 slot = s_history_count % CONSOLE_HISTORY_MAX;
                usize copy_l = s_line_len < (CONSOLE_IN_BUF_SIZE - 1) ? s_line_len : (CONSOLE_IN_BUF_SIZE - 1);
                __builtin_memcpy(s_history[slot], (const char *)s_line_buf, copy_l);
                s_history[slot][copy_l] = '\0';
                s_history_count++;
            }
        }
        s_history_idx = -1;
        s_saved_len = 0;

        if (s_line_len < CONSOLE_IN_BUF_SIZE - 2) {
            s_line_buf[s_line_len++] = '\n';
        }
        to_echo = '\n';
        s_line_ready = true;
    } else if (c == '\t') {
        if (s_line_len < CONSOLE_IN_BUF_SIZE - 2) {
            s_line_buf[s_line_len++] = '\t';
        }
        to_echo = ' ';
    } else if ((u8)c >= 0x20 && (u8)c != 0x7F) {
        if (s_line_len < CONSOLE_IN_BUF_SIZE - 2) {
            s_line_buf[s_line_len++] = c;
            to_echo = c;
        }
    }

    spinlock_unlock_irqrestore(&s_in_lock, irq);
    if (to_echo) {
        console_putc(to_echo);
    }
}

// poll serial RX port COM1
static void console_poll_serial(void) {
    if (inb(COM1_PORT + 5) & 0x01) {
        u8 byte = inb(COM1_PORT);
        console_in_push((char)byte);
    }
}

extern void scheduler_yield(void);
extern void xiukit_hid_poll(void);

i64 console_read(char *dst, usize len) {
    if (!dst || len == 0) return 0;

    if (s_raw_mode) {
        // raw mode read
        while (s_raw_count == 0) {
            console_poll_serial();
            xiukit_hid_poll();
            if (s_raw_count > 0) break;
            scheduler_yield();
        }

        irq_flags_t irq = spinlock_lock_irqsave(&s_in_lock);
        usize read_bytes = 0;
        while (read_bytes < len && s_raw_count > 0) {
            dst[read_bytes++] = s_raw_buf[s_raw_head];
            s_raw_head = (s_raw_head + 1) % CONSOLE_RAW_BUF_SIZE;
            s_raw_count--;
        }
        spinlock_unlock_irqrestore(&s_in_lock, irq);
        return (i64)read_bytes;
    }

    // canonical line mode read
    while (!s_line_ready) {
        console_poll_serial();
        xiukit_hid_poll();
        if (s_line_ready) break;
        scheduler_yield();
    }

    irq_flags_t irq = spinlock_lock_irqsave(&s_in_lock);
    usize to_copy = s_line_len < len ? s_line_len : len;

    for (usize i = 0; i < to_copy; i++) {
        dst[i] = s_line_buf[i];
    }

    // shift remainder if len was smaller than line
    if (to_copy < s_line_len) {
        usize rem = s_line_len - to_copy;
        for (usize i = 0; i < rem; i++) {
            s_line_buf[i] = s_line_buf[to_copy + i];
        }
        s_line_len = rem;
    } else {
        s_line_len = 0;
        s_line_ready = false;
    }

    spinlock_unlock_irqrestore(&s_in_lock, irq);
    return (i64)to_copy;
}

void console_set_raw_mode(bool raw) {
    irq_flags_t irq = spinlock_lock_irqsave(&s_in_lock);
    s_raw_mode = raw;
    if (!raw) {
        s_raw_head = 0;
        s_raw_tail = 0;
        s_raw_count = 0;
    }
    spinlock_unlock_irqrestore(&s_in_lock, irq);
}

typedef struct xiu_termios_raw {
    u32 c_iflag;
    u32 c_oflag;
    u32 c_cflag;
    u32 c_lflag;
    u8  c_cc[32];
} xiu_termios_raw_t;

typedef struct xiu_winsize_raw {
    u16 ws_row;
    u16 ws_col;
    u16 ws_xpixel;
    u16 ws_ypixel;
} xiu_winsize_raw_t;

xiu_error_t console_ioctl(u64 cmd, xiu_vaddr_t arg) {
    extern xiu_error_t copyout(const void *kaddr, void *uaddr, usize len);
    extern xiu_error_t copyin(const void *uaddr, void *kaddr, usize len);

    if (cmd == 0x5401 || cmd == 0x402c7413 || cmd == 0x40487413) {
        xiu_termios_raw_t t;
        __builtin_memset(&t, 0, sizeof(t));
        t.c_lflag = s_raw_mode ? 0 : (0x0002 | 0x0008 | 0x0001);
        return copyout(&t, (void *)arg, sizeof(t));
    }
    if (cmd == 0x5402 || cmd == 0x5403 || cmd == 0x5404 ||
        cmd == 0x802c7414 || cmd == 0x802c7415 || cmd == 0x802c7416) {
        xiu_termios_raw_t t;
        if (copyin((const void *)arg, &t, sizeof(t)) != XIU_SUCCESS) return XIU_ERR_INVALID;
        console_set_raw_mode((t.c_lflag & 0x0002) == 0);
        return XIU_SUCCESS;
    }
    if (cmd == 0x5413 || cmd == 0x40087468) {
        xiu_winsize_raw_t ws;
        ws.ws_row = 25;
        ws.ws_col = 80;
        ws.ws_xpixel = 1280;
        ws.ws_ypixel = 800;
        return copyout(&ws, (void *)arg, sizeof(ws));
    }
    return XIU_ERR_NOTSUP;
}

static inline void log_putc(char c) {
    console_putc(c);
}

void kvprintf(const char *fmt, va_list args) {
    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            log_putc(*p);
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
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s) log_putc(*s++);
                break;
            }
            case 'u':
            case 'd': {
                u64 u = 0;
                if (is_long_long) u = va_arg(args, u64);
                else if (is_long || is_size_t) u = va_arg(args, unsigned long);
                else u = va_arg(args, u32);
                
                char buf[32];
                int i = 31;
                buf[i--] = '\0';
                if (u == 0) buf[i--] = '0';
                else {
                    while (u > 0) {
                        buf[i--] = (u % 10) + '0';
                        u /= 10;
                    }
                }
                while (i >= 0 && (30 - i < width)) buf[i--] = pad_zero ? '0' : ' ';
                for (int k = i + 1; buf[k]; k++) log_putc(buf[k]);
                break;
            }
            case 'x':
            case 'p': {
                u64 val = 0;
                if (*p == 'p') {
                    val = (uptr)va_arg(args, void *);
                    width = 16;
                    pad_zero = true;
                    log_putc('0');
                    log_putc('x');
                } else {
                    if (is_long_long) val = va_arg(args, u64);
                    else if (is_long || is_size_t) val = va_arg(args, unsigned long);
                    else val = va_arg(args, u32);
                }
                
                if (width == 0) {
                    char buf[32];
                    int i = 31;
                    buf[i--] = '\0';
                    if (val == 0) buf[i--] = '0';
                    else {
                        while (val > 0) {
                            int nibble = val & 0xf;
                            buf[i--] = nibble < 10 ? nibble + '0' : nibble - 10 + 'a';
                            val >>= 4;
                        }
                    }
                    for (int k = i + 1; buf[k]; k++) log_putc(buf[k]);
                } else {
                    for (int k = (width - 1) * 4; k >= 0; k -= 4) {
                        int nibble = (val >> k) & 0xf;
                        log_putc(nibble < 10 ? nibble + '0' : nibble - 10 + 'a');
                    }
                }
                break;
            }
            case '%':
                log_putc('%');
                break;
            default:
                log_putc('%');
                log_putc(*p);
                break;
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}
