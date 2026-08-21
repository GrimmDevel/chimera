/*
 * Copyright (C) 2024 Zoe Knox <zoe@ravynsoft.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#import "BSDFramebuffer.h"
#import <sys/types.h>
#import <sys/ipc.h>
#import <sys/shm.h>
#import "rpc.h" // for mode definitions

@implementation BSDFramebuffer

- (id)init
{
    self = [super init];
    _flags = kWSDisplayActive | kWSDisplayOnline | kWSDisplayPrimary | kWSDisplayMain;
    _openGLMask = 0;
    fbfd = -1;
    stride = -1;
    data = NULL;
    size = 0;
    ctx = NULL;
    ctx2 = NULL;
    return self;
}

#define FBIOGET_INFO 0x4601
struct xiu_fb_info {
    unsigned int width;
    unsigned int height;
    unsigned int pitch;
    unsigned int bpp;
    unsigned int format;
    unsigned int vram_size;
};

- (int)openFramebuffer: (const char *)device
{
    printf("[BSDFramebuffer] Opening framebuffer device %s...\n", device);
    fflush(stdout);
    fbfd = open(device, O_RDWR);
    if(fbfd < 0) {
        fbfd = open("/dev/fb0", O_RDWR);
    }
    if(fbfd < 0) {
        printf("[BSDFramebuffer] ERROR: Cannot open framebuffer device (%s): errno=%d\n", device, errno);
        fflush(stdout);
        return -1;
    }

    struct xiu_fb_info xinfo;
    if(ioctl(fbfd, FBIOGET_INFO, &xinfo) == 0 && xinfo.width > 0) {
        width = xinfo.width;
        height = xinfo.height;
        depth = xinfo.bpp ? xinfo.bpp : 32;
        stride = (xinfo.pitch >= xinfo.width * 4) ? xinfo.pitch : xinfo.width * 4;
        printf("[BSDFramebuffer] FBIOGET_INFO: %dx%d, pitch=%d, bpp=%d\n", width, height, stride, depth);
        fflush(stdout);
    } else {
        struct fbtype fb;
        if(ioctl(fbfd, FBIOGTYPE, &fb) < 0 || ioctl(fbfd, FBIO_GETLINEWIDTH, &stride) < 0) {
            printf("[BSDFramebuffer] ERROR: ioctl FBIOGET_INFO / FBIOGTYPE failed: errno=%d\n", errno);
            fflush(stdout);
            close(fbfd);
            return -1;
        }
        depth = fb.fb_depth;
        width = fb.fb_width;
        height = fb.fb_height;
    }

    _currentMode->width = width;
    _currentMode->height = height;
    _currentMode->depth = depth;
    _currentMode->refresh = 60;
    _currentMode->flags = 0;

    CFArrayAppendValue(_allModes, CGDisplayModeRetain(_currentMode));

    size_t pagemask = getpagesize() - 1;
    size = (stride * height + pagemask) & ~pagemask;
    printf("[BSDFramebuffer] mmap framebuffer: size=%zu bytes\n", size);
    fflush(stdout);
    data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_NOCORE|MAP_NOSYNC, fbfd, 0);

    if(data == MAP_FAILED) {
        printf("[BSDFramebuffer] ERROR: mmap framebuffer failed: errno=%d\n", errno);
        fflush(stdout);
        return -1;
    }

    printf("[BSDFramebuffer] Framebuffer mapped at %p (size=%zu)\n", data, size);
    fflush(stdout);

    cs = CGColorSpaceCreateDeviceRGB();
    ctxPixels = calloc(1, size);
    ctx2Pixels = calloc(1, size);
    ctx = [O2Context createWithBytes:ctxPixels width:width height:height 
                bitsPerComponent:8 bytesPerRow:stride colorSpace:(__bridge O2ColorSpaceRef)cs
                bitmapInfo:[self format] releaseCallback:NULL releaseInfo:NULL];
    if(!ctx) ctx = [O2Context new];
    ctx2 = [O2Context createWithBytes:ctx2Pixels width:width height:height 
            bitsPerComponent:8 bytesPerRow:stride colorSpace:(__bridge O2ColorSpaceRef)cs
            bitmapInfo:[self format] releaseCallback:NULL releaseInfo:NULL];
    if(!ctx2) ctx2 = [O2Context new];
    activeCtx = ctx;
    printf("[BSDFramebuffer] Onyx2D graphics contexts initialized (ctx=%p, ctxPixels=%p)\n", ctx, ctxPixels);
    fflush(stdout);
    return 0;
}

- (void)dealloc
{
    [self releaseCapture];
    if(fbfd >= 0) {
        munmap(data, size);
        close(fbfd);
    }
    if(ctxPixels) {
        free(ctxPixels);
        ctxPixels = NULL;
    }
    if(ctx2Pixels) {
        free(ctx2Pixels);
        ctx2Pixels = NULL;
    }
    if(cs)
        CGColorSpaceRelease(cs);
    ctx = nil;
    activeCtx = nil;
    ctx2 = nil;
}

// clear screen. does not swap active buffer
-(void)clear {
    void *pixels = NULL;
    if(_captured && captureCtx)
        pixels = [[captureCtx surface] pixelBytes];
    else 
        pixels = ctxPixels;

    if(!pixels) pixels = data;

    if(pixels && size > 0) {
        uint32_t *p = (uint32_t *)pixels;
        size_t count = size / 4;
        for(size_t i = 0; i < count; i++) {
            p[i] = 0xFF1C2230; // Sleek Mac dark blue-gray desktop wallpaper
        }
        if(pixels != data && data) {
            memcpy(data, pixels, size);
        }
    }
}

- (void)draw
{
    static uint64_t s_draw_calls = 0;
    s_draw_calls++;
    void *pixels = 0;
    if(_captured && captureCtx)
        pixels = [[captureCtx surface] pixelBytes];
    else
        pixels = ctxPixels;
    if(!pixels) pixels = ctxPixels;
    if(data && pixels && pixels != data && size > 0) {
        memcpy(data, pixels, size);
    }
    if (s_draw_calls == 1 || (s_draw_calls % 60) == 0) {
        printf("[BSDFramebuffer] draw #%llu: blitted %zu bytes (from %p to VRAM %p, %dx%d, stride=%d, captured=%u)\n",
               s_draw_calls, size, pixels, data, width, height, stride, _captured);
        fflush(stdout);
    }
}

- (void)drawWithCursor:(O2Image *)cursor inRect:(O2Rect)rect {
    if(ctx2Pixels && ctxPixels && size > 0) {
        memcpy(ctx2Pixels, ctxPixels, size);
    }
    if(data && ctx2Pixels && size > 0) {
        memcpy(data, ctx2Pixels, size);
    }
}

// return the context for drawing, i.e. the back buffer
- (O2Context *)context
{
    if(_captured)
        return captureCtx;
    else
        return ctx;
}

-(BOOL)capture:(pid_t)pid withOptions:(uint32_t)options {
    if(_captured != 0)
        return NO;

    pthread_mutex_lock(&renderLock);
    _captured = pid;

    int reserved = 6*sizeof(int); // save space for dimensions info 
    shmSize = size + reserved;
    shmid = shmget([self getDisplayID] ^ random(), shmSize, IPC_CREAT|0666);
    if(shmid == 0)
        return NO;

    uint8_t *p = shmat(shmid, NULL, 0);
    if(!p) {
        shmctl(shmid, IPC_RMID, NULL);
        shmid = 0;
        return NO;
    }

    uint8_t *bufaddr = (p + reserved);
    captureCtx = [[O2Context_builtin alloc] initWithBytes:(void *)bufaddr
                width:width height:height bitsPerComponent:8 bytesPerRow:width*4
                colorSpace:(__bridge O2ColorSpaceRef)cs
                bitmapInfo:[self format] releaseCallback:NULL releaseInfo:NULL];
    activeCtx = captureCtx;
    pthread_mutex_unlock(&renderLock);
    intptr_t *q = (intptr_t *)p;
    q[0] = width;
    q[1] = height;
    q[2] = [self format];
    
    // we ignore the deprecated options and always fill with black
    [self clear];
    return YES;
}

-(void)releaseCapture {
    pthread_mutex_lock(&renderLock);
    _captured = 0;
    activeCtx = ctx;
    if(captureCtx != nil) {
        shmctl(shmid, IPC_RMID, NULL);
        void *buffer = [[captureCtx surface] pixelBytes];
        buffer -= 6*sizeof(int);
        shmdt(buffer);
        shmid = 0;
        shmSize = 0;
    }
    captureCtx = nil;
    pthread_mutex_unlock(&renderLock);
}

/* FIXME: this should hash the vendor, model, serial, and other data */
- (uint32_t)getDisplayID {
    return 0xf07f0a10; // arbitrary ID
}

// we can't change resolutions without a drm driver so this will always fail
-(BOOL)setMode:(struct CGDisplayMode *)mode {
    return NO;
}

-(void *)pixels {
    return ctxPixels ? ctxPixels : data;
}

-(void *)vram {
    return data;
}

-(int)stride {
    return stride;
}

-(int)width {
    return width;
}

-(int)height {
    return height;
}

@end
