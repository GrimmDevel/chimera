#ifndef _SYS_FBIO_H_
#define _SYS_FBIO_H_

#include <sys/types.h>

#ifndef FBIOGET_INFO
#define FBIOGET_INFO      0x4601
#endif
#define FBIOGTYPE         0x4602
#define FBIO_GETLINEWIDTH 0x4603

struct fbtype {
    int fb_type;
    int fb_height;
    int fb_width;
    int fb_depth;
    int fb_cmsize;
    int fb_size;
};

struct video_info {
    int vi_width;
    int vi_height;
    int vi_depth;
    int vi_line_bytes;
    void *vi_buffer;
};

#endif
