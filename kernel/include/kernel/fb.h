// framebuffer info
#ifndef CHIMERA_FB_H
#define CHIMERA_FB_H

#include <kernel/chimera_types.h>

struct fb_info {
    u32 width;
    u32 height;
    u32 pitch;
    u32 bpp;
    u32 format;
    u32 vram_size;
};

struct fb_pan_info {
    u32 xoffset;
    u32 yoffset;
};

#define FBIOGET_INFO      0x4601
#define FBIOPAN_DISPLAY   0x4602

#endif
