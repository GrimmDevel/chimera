/*
 * XIU Operating System — Darwin XNU Video Console Header
 * Adapted from Apple XNU osfmk/console/video_console.h
 */

#ifndef _KERNEL_VIDEO_CONSOLE_H_
#define _KERNEL_VIDEO_CONSOLE_H_

#include <kernel/xiu_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct vc_info {
  unsigned int v_height;    /* pixels */
  unsigned int v_width;     /* pixels */
  unsigned int v_depth;     /* bits per pixel */
  unsigned int v_rowbytes;  /* bytes per scanline */
  unsigned long v_baseaddr; /* virtual address of framebuffer */
  unsigned int v_type;
  char v_name[32];
  uint64_t v_physaddr;
  unsigned int v_rows;    /* characters */
  unsigned int v_columns; /* characters */
  unsigned int v_rowscanbytes;
  unsigned int v_scale;
  unsigned int v_rotate;
  unsigned int v_reserved[3];
};

typedef struct vc_info vc_info_t;

extern unsigned char iso_font[256 * 16];

void video_console_init(unsigned long baseaddr, uint64_t physaddr,
                        unsigned int width, unsigned int height,
                        unsigned int depth, unsigned int pitch);
void video_console_init_backbuffer(void);

void vc_putchar(char ch);
void vc_puts(const char *s);
void vc_write(const char *s, usize len);
void vc_putc_raw(char c);

void vc_scroll_up(int num_rows);
void vc_scroll_down(int num_rows);
void vc_clear_screen(void);
void vc_reverse_cursor(void);

void vc_set_raw_mode(bool raw);
bool vc_get_raw_mode(void);

struct vc_info *vc_get_info(void);

#ifdef __cplusplus
}
#endif

#endif /* _KERNEL_VIDEO_CONSOLE_H_ */
