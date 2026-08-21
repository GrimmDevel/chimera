/*
 * Copyright (C) 2022-2025 Zoe Knox <zoe@pixin.net>
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

#define WINDOWSERVER 1

#import <Onyx2D/O2Context.h>
#import <Onyx2D/O2Surface.h>
#import <Onyx2D/O2Image.h>
#import <Onyx2D/O2ImageSource.h>
#import <AppKit/NSAttributedString.h>
#import <AppKit/NSColor.h>
#import <AppKit/NSFont.h>
#import <AppKit/NSWindow.h>
#import <AppKit/NSProgressIndicator.h>
#import <AppKit/NSImageView.h>
#import <AppKit/NSGraphicsContext.h>
#import "common.h"
#import "WindowServer.h"
#include <ws_proto.h>

#define _cursor_height 24

#undef direction // defined in mach.h
#include <linux/input.h>

#include <poll.h>
#include <kvm.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <launch.h>

#import "rpc.h"
#import "ws_font.h"

/* Desktop Environment State & Built-in Applications */
static BOOL s_term_open = YES;
static NSRect s_term_frame = {100, 70, 580, 360};
static BOOL s_calc_open = YES;
static NSRect s_calc_frame = {730, 100, 240, 330};
static BOOL s_about_open = NO;
static NSRect s_about_frame = {450, 220, 380, 220};
static int s_active_window = 1; // 1: term, 2: calc, 3: about, 0: client
static BOOL s_apple_menu_open = NO;
static int s_calc_val = 0;
static int s_calc_acc = 0;
static int s_calc_op = 0;
static char s_calc_str[32] = "0";
static BOOL s_in_drag = NO;
static int s_drag_win = 0;
static int s_drag_off_x = 0;
static int s_drag_off_y = 0;

/* This lock prevents other threads from messing with the graphics context while we
 * are in the rendering loop
 */
pthread_mutex_t renderLock;

@implementation WindowServer

static void notifyAppExited(mach_port_t port, pid_t pid, const char *bundleID, const char *path) {
    Message msg = {0};
    msg.header.msgh_remote_port = port;
    msg.header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND, 0, 0, 0);
    msg.header.msgh_id = MSG_ID_INLINE;
    msg.header.msgh_size = sizeof(msg) - sizeof(mach_msg_trailer_t);
    msg.code = CODE_APP_EXITED;
    msg.pid = pid;
    msg.len = strlen(path);
    strncpy(msg.data, path, PATH_MAX);
    strncpy(msg.bundleID, bundleID, sizeof(msg.bundleID));
    mach_msg((mach_msg_header_t *)&msg, MACH_SEND_MSG|MACH_SEND_TIMEOUT,
            sizeof(msg) - sizeof(mach_msg_trailer_t),
            0, MACH_PORT_NULL, 100 /* ms timeout */, MACH_PORT_NULL);
}

static NSString *_pathForPID(pid_t pid) {
    int mib[4];
    char buf[PATH_MAX+1];
    size_t len = PATH_MAX;

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PATHNAME;
    mib[3] = pid;

    if(sysctl(mib, 4, buf, &len, NULL, 0) == 0 && buf[0] != '\0') {
        return [NSString stringWithCString:buf];
    }

    if (pid == 2) return @"/System/Library/CoreServices/WindowServer";
    if (pid == 3) return @"/System/Library/CoreServices/SystemUIServer";
    if (pid == 4 || pid == 5) return @"/System/Library/CoreServices/Dock";

    return @"/bin/app";
}

-init {
    printf("[WindowServer] -init: Initializing WindowServer core...\n");
    fflush(stdout);
    ready = NO;
    logLevel = WS_ERROR;
    envp = NULL;
    curShell = LOADING;
    curApp = nil;
    curWindow = nil;
    pthread_mutex_init(&renderLock, NULL);
    cursorHideCount = 0;

    kern_return_t kr;
    printf("[WindowServer] -init: Checking in Mach service %s...\n", WINDOWSERVER_SVC_NAME);
    fflush(stdout);
    if((kr = bootstrap_check_in(bootstrap_port, WINDOWSERVER_SVC_NAME, &_servicePort)) != KERN_SUCCESS) {
        printf("[WindowServer] -init: bootstrap_check_in failed: %d\n", kr);
        fflush(stdout);
        return nil;
    }
    printf("[WindowServer] -init: Mach service checked in (service=%s, port=0x%x)\n", WINDOWSERVER_SVC_NAME, _servicePort);
    fflush(stdout);

    _kq = kqueue();
    if(_kq < 0) {
        printf("[WindowServer] -init: kqueue failed: %d\n", errno);
        fflush(stdout);
        return nil;
    }

    displays = [NSMutableArray new];
    apps = [NSMutableDictionary new];

    for(int i = 0; i < kCGNumReservedWindowLevels; ++i)
        _windows[i] = [NSMutableArray new];

    printf("[WindowServer] -init: Initializing input subsystem (WSInput)...\n");
    fflush(stdout);
    input = [WSInput new];
    [input setLogLevel:logLevel];

    printf("[WindowServer] -init: Initializing BSDFramebuffer (/dev/fb0)...\n");
    fflush(stdout);
    fb = [BSDFramebuffer new];
    if([fb openFramebuffer:"/dev/fb0"] < 0) {
        printf("[WindowServer] -init: Failed to open framebuffer /dev/fb0!\n");
        fflush(stdout);
        return nil;
    }
    _geometry = [fb geometry];
    printf("[WindowServer] -init: fb=%p class=%s, displays=%p class=%s\n",
           fb, object_getClassName(fb), displays, object_getClassName(displays));
    fflush(stdout);
    [displays addObject:fb];
    printf("[WindowServer] -init: displays=%p, count=%lu\n", displays, (unsigned long)[displays count]);
    printf("[WindowServer] -init: Framebuffer geometry %dx%d\n", (int)_geometry.size.width, (int)_geometry.size.height);
    fflush(stdout);

    [fb clear];

    [input setGeometry:_geometry];
    [input setPointerPos:NSMakePoint(_geometry.size.width / 2, _geometry.size.height / 2)];

    ready = YES;
    printf("[WindowServer] -init: WindowServer initialized and ready!\n");
    fflush(stdout);
    return self;
}

-(void)dealloc {
    curShell = NONE;
    fb = nil;
    input = nil;
    if(kvm)
        kvm_close(kvm);
}

-(void)setLogLevel:(int)level {
    logLevel = level;
    [input setLogLevel:level];
}

-(void)setDebugLevel:(int)level subsystem:(char)sys {
    id obj = nil;
    switch(sys) {
        case 'i': obj = input; break;
    }
    if(obj)
        [obj setDebugLevel:level];
}

-(BOOL)isReady {
    return ready;
}

-(O2BitmapContext *)context {
    return [fb context];
}

-(NSRect)geometry {
    return _geometry;
}

-(void)draw {
    return [fb draw];
}

-(WSDisplay *)displayWithID:(uint32_t)ID {
    printf("[WindowServer displayWithID] searching for ID 0x%x among %lu displays...\n", ID, (unsigned long)[displays count]);
    fflush(stdout);
    for(int i = 0; i < [displays count]; ++i) {
        WSDisplay *d = [displays objectAtIndex:i];
        printf("[WindowServer displayWithID] display[%d]=%p, getDisplayID=0x%x\n", i, d, [d getDisplayID]);
        fflush(stdout);
        if([d getDisplayID] == ID)
            return d;
    }
    return nil;
}

-(BOOL)setUpEnviron:(uid_t)uid {
    struct passwd *pw = getpwuid(uid);
    if(!pw)
        return NO;
    int entries = 7;
    envp = malloc(sizeof(char *) * entries);
    asprintf(&envp[0], "HOME=%s", pw->pw_dir);
    asprintf(&envp[1], "SHELL=%s", pw->pw_shell);
    asprintf(&envp[2], "USER=%s", pw->pw_name);
    asprintf(&envp[3], "LOGNAME=%s", pw->pw_name);
    asprintf(&envp[4], "PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin");
    asprintf(&envp[6], "TERM=xterm");
    envp[entries - 1] = NULL;
    return YES;
}

-(void)freeEnviron {
    if(envp == NULL)
        return;

    while(*envp != NULL)
        free(*envp++);
    free(envp);
}

-(uint32_t)windowCreate:(struct wsRPCWindow *)data forApp:(WSAppRecord *)app {
    struct kinfo_proc *kp;

    if(data->state < 0 || data->state >= WIN_STATE_MAX) {
        NSLog(@"windowCreate called with invalid state");
        data->state = NORMAL;
    }

    if([app windowWithID:data->windowID] != nil) {
        NSLog(@"windowCreate cannot create existing window ID %u for %@", data->windowID, app);
        return 0;
    }

    WSWindowRecord *winrec = [WSWindowRecord new];
    winrec.app = app;
    winrec.number = data->windowID;
    winrec.state = data->state;
    winrec.styleMask = data->style;
    winrec.geometry = NSMakeRect(data->x, data->y, data->w, data->h); // FIXME: bounds check?
    if([app.bundleID isEqualToString:@"com.ravynos.LoginWindow"])
        winrec.level = kCGOverlayWindowLevelKey;
    else if(data->level >= kCGMinimumWindowLevelKey && data->level <= kCGMaximumWindowLevelKey)
        winrec.level = data->level;
    int len = 0;
    while(data->title[len] != '\0' && len < sizeof(data->title)) ++len;
    winrec.title = [NSString stringWithCString:data->title length:len];
    winrec.icon = nil;

    winrec.shmPath = [NSString stringWithFormat:@"/%@/%u/win/%u", [app bundleID],
        [app pid], winrec.number];
    winrec.bufSize = ([fb getDepth]/8) * data->w * data->h;

    int shmfd = shm_open([winrec.shmPath cString], O_RDWR|O_CREAT, 0600);
    if(shmfd < 0) {
        NSLog(@"Cannot open shm fd: %s", strerror(errno));
        return 0;
    }

    if(ftruncate(shmfd, winrec.bufSize) < 0)
        NSLog(@"shmfd ftruncate failed: %s", strerror(errno));

    int count = 0;
    if(kvm != NULL) {
        kp = kvm_getprocs(kvm, KERN_PROC_PID, [app pid], &count);
        if(kp != NULL && count == 1 && kp->ki_pid == [app pid]) {
            if(fchown(shmfd, kp->ki_uid, kp->ki_rgid) < 0)
                NSLog(@"shmfd fchown failed: %s", strerror(errno));
        }
    }

    winrec.surfaceBuf = mmap(NULL, winrec.bufSize, PROT_WRITE|PROT_READ, MAP_SHARED|MAP_NOCORE, shmfd, 0);
    close(shmfd);

    if(winrec.surfaceBuf == NULL) {
        winrec.bufSize = 0;
        NSLog(@"Cannot alloc surface memory! %s", strerror(errno));
        return 0;
    }

    winrec.surface = [[O2Surface alloc] initWithBytes:winrec.surfaceBuf width:data->w
            height:data->h bitsPerComponent:8 bytesPerRow:4*(data->w)
            colorSpace:[fb colorSpace]
            bitmapInfo:kCGBitmapByteOrderDefault|kCGImageAlphaPremultipliedFirst];
    winrec.frame = winrec.geometry;

    [app addWindow:winrec];
    [self addWindowByLevel:winrec];
    if(curApp == app)
        curWindow = winrec; // FIXME: is this how macOS behaves?
    if(logLevel >= WS_INFO)
        NSLog(@"windowCreate: success! %@", winrec);
    return winrec.number;
}

-(void)windowModify:(struct wsRPCWindow *)data forApp:(WSAppRecord *)app {
    if(data->state < 0 || data->state >= WIN_STATE_MAX) {
        NSLog(@"windowModify called with invalid state");
        data->state = NORMAL;
    }

    WSWindowRecord *winrec = [app windowWithID:data->windowID];
    int oldState = winrec.state;
    winrec.state = data->state;
    winrec.styleMask = data->style;
    NSRect oldFrame = winrec.geometry;
    winrec.geometry = NSMakeRect(data->x, data->y, data->w, data->h); // FIXME: bounds check?
    if(!NSEqualRects(winrec.geometry, oldFrame)) {
        // it resized, so fix up our surface to match
        pthread_mutex_lock(&renderLock);
        winrec.bufSize = ([fb getDepth]/8) * data->w * data->h;

        int shmfd = shm_open([winrec.shmPath cString], O_RDWR|O_CREAT, 0600);
        if(shmfd < 0) {
            NSLog(@"Cannot open shm fd: %s", strerror(errno));
            return;
        }

        if(ftruncate(shmfd, winrec.bufSize) < 0)
            NSLog(@"shmfd ftruncate failed: %s", strerror(errno));

        int count = 0;
        struct kinfo_proc *kp;
        kp = kvm_getprocs(kvm, KERN_PROC_PID, [app pid], &count);
        if(count != 1 || kp->ki_pid != [app pid]) {
            NSLog(@"Cannot get client task info! pid %u", [app pid]);
            return;
        }

        if(fchown(shmfd, kp->ki_uid, kp->ki_rgid) < 0)
            NSLog(@"shmfd fchown failed: %s", strerror(errno));

        winrec.surfaceBuf = mmap(NULL, winrec.bufSize, PROT_WRITE|PROT_READ,
                MAP_SHARED|MAP_NOCORE, shmfd, 0);
        close(shmfd);

        if(winrec.surfaceBuf == NULL) {
            winrec.bufSize = 0;
            NSLog(@"Cannot alloc surface memory! %s", strerror(errno));
            return;
        }

        winrec.surface = [[O2Surface alloc] initWithBytes:winrec.surfaceBuf width:data->w
            height:data->h bitsPerComponent:8 bytesPerRow:4*(data->w)
            colorSpace:[fb colorSpace]
            bitmapInfo:kCGBitmapByteOrderDefault|kCGImageAlphaPremultipliedFirst];
        pthread_mutex_unlock(&renderLock);
    }

    int len = 0;
    while(data->title[len] != '\0' && len < sizeof(data->title)) ++len;
    winrec.title = [NSString stringWithCString:data->title length:len];
    winrec.icon = nil;
    winrec.frame = winrec.geometry;

    if(![app.bundleID isEqualToString:@"com.ravynos.LoginWindow"]) {
        if(data->level >= kCGMinimumWindowLevelKey && data->level <= kCGMaximumWindowLevelKey
            && data->level != winrec.level) {
            [self removeWindowByLevel:winrec];
            winrec.level = data->level;
            [self addWindowByLevel:winrec];
        }
    }

    if(logLevel >= WS_INFO)
        NSLog(@"windowModify %@ win %@", app, winrec);

    if(oldState == MINIMIZED && winrec.state != MINIMIZED)
        [self notifyDock:data length:sizeof(struct wsRPCWindow) 
                withCode:CODE_WINDOW_STATE forApp:app];
}

-(void)addWindowByLevel:(WSWindowRecord *)window {
    if(logLevel >= WS_INFO)
        NSLog(@"addWindowByLevel %@", window);
    [_windows[window.level] addObject:window];
}

-(void)removeWindowByLevel:(WSWindowRecord *)window {
    if(logLevel >= WS_INFO)
        NSLog(@"removeWindowByLevel %@", window);
    pthread_mutex_lock(&renderLock);
    [_windows[window.level] removeObject:window];
    pthread_mutex_unlock(&renderLock);
}

-(void)removeWindowFromAllLevels:(WSWindowRecord *)window {
    if(logLevel >= WS_INFO)
        NSLog(@"removeWindowFromAllLevels %@", window);
    pthread_mutex_lock(&renderLock);
    for(int i = 0; i < kCGNumReservedWindowLevels; ++i)
        [_windows[window.level] removeObject:window];
    pthread_mutex_unlock(&renderLock);
}

-(void)setShell:(int)shell {
    curShell = shell;
}

-(void)launchShell:(id)object {
    int status;
    siginfo_t siginfo = {0};
    NSString *lwPath = nil;
    uid_t uid = 0;
    gid_t gid = 0;

    while(curShell != NONE) {
        switch(curShell) {
            case LOADING: {
                lwPath = [[NSBundle mainBundle] pathForResource:@"LoadingWindow" ofType:@"app"];
                lwPath = [[NSBundle bundleWithPath:lwPath] executablePath];
                if(!lwPath) {
                    NSLog(@"missing LoginWindow.app!");
                    curShell = LOGINWINDOW;
                    break;
                }

                pid_t pid = fork();
                if(!pid) { // child
                    setuid(65534); // nobody
                    execle([lwPath UTF8String], [[lwPath lastPathComponent] UTF8String], NULL, NULL);
                    exit(1);
                } else if(pid < 0) {
                    NSLog(@"LoadingWindow fork() failed");
                    break;
                }

                waitpid(pid, &status, WEXITED);
                if(WIFEXITED(status) && WEXITSTATUS(status) == 0)
                    curShell = LOGINWINDOW;
                break;
            }
            case LOGINWINDOW:
                // FIXME: can we use load_launchd_jobs_at_loginwindow_prompt() here?
                lwPath = [[NSBundle mainBundle] pathForResource:@"LoginWindow" ofType:@"app"];
                if(!lwPath) {
                    NSLog(@"missing LoginWindow.app!");
                    sleep(1);
                    break;
                }
                lwPath = [[NSBundle bundleWithPath:lwPath] executablePath];
                if(!lwPath) {
                    NSLog(@"missing LoginWindow.app!");
                    sleep(1);
                    break;
                }

                int fds[2];
                pipe(fds);
                pid_t pid = fork();
                if(!pid) { // child
                    close(fds[0]);
                    char fdbuf[12];
                    sprintf(fdbuf, "%d", fds[1]);
                    setuid(65534); // nobody
                    execle([lwPath UTF8String], [[lwPath lastPathComponent] UTF8String], fdbuf, NULL, NULL);
                    exit(1);
                } else {
                    /* While it's running, try to read creds from the pipe. Validate them and
                     * set uid if correct. Return error if not and continue
                     */
                    close(fds[1]);

                    fd_set readfds;
                    FD_ZERO(&readfds);
                    FD_SET(fds[0], &readfds);
                    struct timeval tv = {0, 100000};
                    char credbuf[64];
                    BOOL loggedIn = NO;

                    while(!loggedIn && waitpid(pid, &status, WEXITED|WNOHANG) == 0) {
                        FD_SET(fds[0], &readfds);
                        int ret = select(fds[0] + 1, &readfds, NULL, NULL, &tv);
                        switch(ret) {
                            case -1: perror("select");
                                     kill(pid, SIGTERM);
                                     close(fds[0]);
                                     break;
                            case 0: continue;
                        }
                        int bytes = read(fds[0], credbuf, sizeof(credbuf));
                        credbuf[bytes - 1] = 0;
                        char *pass = &credbuf[strlen(credbuf) + 1];
                        if(pass > (credbuf + bytes)) {
                            NSLog(@"Malformed auth input");
                            continue;
                        }

                        struct passwd *pw = getpwnam(credbuf);
                        if(!pw) {
                            if(logLevel >= WS_WARNING)
                                NSLog(@"Bad username or password (%s)", credbuf);
                            write(fds[0], "FAIL", 5);
                            continue;
                        }
                        char *enc = crypt(pass, pw->pw_passwd);
                        if(strcmp(enc, pw->pw_passwd)) {
                            if(logLevel >= WS_WARNING)
                                NSLog(@"Bad username or password (%s)", credbuf);
                            write(fds[0], "FAIL", 5);
                            continue;
                        }

                        // it must be valid since we got here!
                        uid = pw->pw_uid;
                        gid = pw->pw_gid;
                        write(fds[0], "AUTH", 5);
                        close(fds[0]);
                        close(fds[1]);
                        kill(pid, SIGTERM);
                        loggedIn = YES;
                    }

                    if(loggedIn && uid != 0) {
                        if(logLevel >= WS_WARNING)
                            NSLog(@"Logged in user %u:%u", uid, gid);
                        curShell = DESKTOP;
                    } else
                        NSLog(@"LoginWindow exited without authenticating");
                    break;
                }
                break;
            case DESKTOP: {
                pid_t pid = fork();
                if(pid == 0) {
                    struct passwd *pw = getpwuid(uid);
                    if(!pw) {
                        NSLog(@"uid not found");
                        curShell = LOGINWINDOW;
                        break;
                    }
                    setlogin(pw->pw_name);
                    chdir(pw->pw_dir);

                    login_cap_t *lc = login_getpwclass(pw);
                    if (setusercontext(lc, pw, pw->pw_uid,
                        LOGIN_SETALL & ~(LOGIN_SETLOGIN)) != 0) {
                            perror("setusercontext");
                            exit(-1);
                    }
                    login_close(lc);

                    NSString *path = [[NSBundle mainBundle] pathForResource:@"SystemUIServer" ofType:@"app"];
                    if(path)
                        path = [[NSBundle bundleWithPath:path] executablePath];

                    if(path) {
                        execle([path UTF8String], [path UTF8String], NULL, NULL);
                    }

                    perror("execl");
                    exit(1);
                } else if(pid < 0) {
                    perror("fork");
                    sleep(1);
                    curShell = LOGINWINDOW;
                    break;
                }

                pid_t exited = 0;
                while((exited = wait(&status)) != pid) ;

                switch(WEXITSTATUS(status)) {
                    case EXIT_RESTART: NSLog(@"should restart!");
                                       curShell = NONE;
                                       kill(1, SIGINT);
                                       break;
                    case EXIT_SHUTDOWN: NSLog(@"should shut down!");
                                        curShell = NONE;
                                        kill(1, SIGUSR2);
                                        break;
                    case EXIT_LOGOUT: [self performLogout:uid];
                                      curShell = LOGINWINDOW;
                                      break;
                }
            }
            break;
        }

    }
    [[NSThread currentThread] cancel];
}

-(void)performLogout:(uid_t)uid {
      WSAppRecord *app;

      NSString *cmd = [NSString stringWithFormat:
          @"/bin/launchctl remove com.apple.launchd.peruser.%d", uid];
      system([cmd UTF8String]);

      NSEnumerator *appEnum = [apps objectEnumerator];
      while((app = [appEnum nextObject]) != nil)
          kill(app.pid, SIGTERM);
}

/*
 * KEEP THIS RUN LOOP EFFICIENT! It is called every frame to render the entire screen contents.
 */
static void ws_render_desktop(WindowServer *self, uint32_t *p, int pitch, int scr_w, int scr_h, NSPoint cursorPos) {
    if (!p || scr_w <= 0 || scr_h <= 0) return;

    // 1. Cosmic Wallpaper Gradient (Monterey / Big Sur aesthetic)
    ws_draw_gradient_v(p, pitch, scr_w, scr_h, 0, 0, scr_w, scr_h, 0xFF0D1B2A, 0xFF1B263B);
    for (int x = 0; x < scr_w; x++) {
        int wave = (x % 360) - 180;
        int gy = scr_h - 220 + (wave * wave) / 1300;
        ws_fill_rect(p, pitch, scr_w, scr_h, x, gy, 1, 60, 0x153A86FF);
        ws_fill_rect(p, pitch, scr_w, scr_h, x, gy + 30, 1, 40, 0x108338EC);
    }

    // 2. Top macOS MenuBar (Height 24px)
    ws_fill_rect(p, pitch, scr_w, scr_h, 0, 0, scr_w, 24, 0xEA161D28);
    ws_fill_rect(p, pitch, scr_w, scr_h, 0, 23, scr_w, 1, 0x33FFFFFF);
    ws_draw_apple_logo(p, pitch, scr_w, scr_h, 14, 5, 0xFFFFFFFF);

    const char *active_app_title = "Finder";
    if (s_active_window == 1 && s_term_open) active_app_title = "Terminal";
    else if (s_active_window == 2 && s_calc_open) active_app_title = "Calculator";
    else if (s_active_window == 3 && s_about_open) active_app_title = "System";

    ws_draw_text_bold(p, pitch, scr_w, scr_h, 34, 4, active_app_title, 0xFFFFFFFF);
    ws_draw_text(p, pitch, scr_w, scr_h, 110, 4, "File", 0xFFD1D5DB);
    ws_draw_text(p, pitch, scr_w, scr_h, 154, 4, "Edit", 0xFFD1D5DB);
    ws_draw_text(p, pitch, scr_w, scr_h, 198, 4, "View", 0xFFD1D5DB);
    ws_draw_text(p, pitch, scr_w, scr_h, 242, 4, "Go", 0xFFD1D5DB);
    ws_draw_text(p, pitch, scr_w, scr_h, 274, 4, "Window", 0xFFD1D5DB);
    ws_draw_text(p, pitch, scr_w, scr_h, 334, 4, "Help", 0xFFD1D5DB);

    // Right Status Items
    ws_draw_text(p, pitch, scr_w, scr_h, scr_w - 270, 4, "100% [||||]", 0xFF9CA3AF);
    ws_draw_text(p, pitch, scr_w, scr_h, scr_w - 175, 4, "[●] WiFi", 0xFF9CA3AF);
    ws_draw_text_bold(p, pitch, scr_w, scr_h, scr_w - 95, 4, "Sat 02:30", 0xFFFFFFFF);

    // 3. Apple Menu Dropdown (if open)
    if (s_apple_menu_open) {
        ws_draw_rounded_rect(p, pitch, scr_w, scr_h, 6, 26, 180, 160, 8, 0xF0181E29, 0x50FFFFFF);
        ws_draw_text(p, pitch, scr_w, scr_h, 18, 34, "About This Mac", 0xFFFFFFFF);
        ws_draw_text(p, pitch, scr_w, scr_h, 18, 52, "System Settings...", 0xFFCBD5E1);
        ws_fill_rect(p, pitch, scr_w, scr_h, 14, 70, 164, 1, 0x33FFFFFF);
        ws_draw_text(p, pitch, scr_w, scr_h, 18, 78, "Terminal", 0xFFFFFFFF);
        ws_draw_text(p, pitch, scr_w, scr_h, 18, 96, "Calculator", 0xFFFFFFFF);
        ws_fill_rect(p, pitch, scr_w, scr_h, 14, 114, 164, 1, 0x33FFFFFF);
        ws_draw_text(p, pitch, scr_w, scr_h, 18, 122, "Restart...", 0xFFCBD5E1);
        ws_draw_text(p, pitch, scr_w, scr_h, 18, 140, "Shut Down...", 0xFFCBD5E1);
    }

    // 4. Built-in Applications

    // --- Terminal Window ---
    if (s_term_open) {
        int tx = (int)s_term_frame.origin.x;
        int ty = (int)s_term_frame.origin.y;
        int tw = (int)s_term_frame.size.width;
        int th = (int)s_term_frame.size.height;
        BOOL is_act = (s_active_window == 1);

        // Shadow
        ws_draw_rounded_rect(p, pitch, scr_w, scr_h, tx - 4, ty - 2, tw + 8, th + 8, 10, 0x50000000, 0);
        // Titlebar
        ws_draw_rounded_rect(p, pitch, scr_w, scr_h, tx, ty, tw, 28, 8, is_act ? 0xFF282E3A : 0xFF1C212B, 0xFF3D4656);
        ws_draw_traffic_lights(p, pitch, scr_w, scr_h, tx + 8, ty + 8, -1);
        ws_draw_text_bold(p, pitch, scr_w, scr_h, tx + 65, ty + 6, "root@Mac: ~ (zsh)", 0xFFE2E8F0);

        // Interior
        ws_fill_rect(p, pitch, scr_w, scr_h, tx, ty + 28, tw, th - 28, 0xFF0D1117);
        // 1px border
        ws_draw_rounded_rect(p, pitch, scr_w, scr_h, tx, ty, tw, th, 8, 0, 0xFF3D4656);

        // Terminal text lines
        int ly = ty + 36;
        ws_draw_text(p, pitch, scr_w, scr_h, tx + 14, ly, "XIU OS 1.0 (Darwin 24.0.0 XNU) - Hybrid Mach/BSD", 0xFF64748B); ly += 18;
        ws_draw_text(p, pitch, scr_w, scr_h, tx + 14, ly, "Kernel: xiu_kernel 1.0.0 (x86_64 Ring 0)", 0xFF64748B); ly += 24;

        ws_draw_text_bold(p, pitch, scr_w, scr_h, tx + 14, ly, "root@Mac ~ # neofetch", 0xFF38BDF8); ly += 20;

        ws_draw_text(p, pitch, scr_w, scr_h, tx + 18, ly, "   .:'      root@Mac", 0xFF10B981); ly += 18;
        ws_draw_text(p, pitch, scr_w, scr_h, tx + 18, ly, "  '::'      OS: XIU OS 1.0 (Darwin 24.0.0)", 0xFF10B981); ly += 18;
        ws_draw_text(p, pitch, scr_w, scr_h, tx + 18, ly, " .:::'      Host: Apple Silicon / Virtual Mac", 0xFF10B981); ly += 18;
        ws_draw_text(p, pitch, scr_w, scr_h, tx + 18, ly, ".::::::.    Kernel: xiu_kernel (Mach-O)", 0xFF10B981); ly += 18;
        ws_draw_text(p, pitch, scr_w, scr_h, tx + 18, ly, ":::::::::   Shell: zsh 5.9", 0xFF10B981); ly += 18;
        ws_draw_text(p, pitch, scr_w, scr_h, tx + 18, ly, ":::::::::   WM: WindowServer (Onyx2D Compositor)", 0xFF10B981); ly += 18;
        ws_draw_text(p, pitch, scr_w, scr_h, tx + 18, ly, " ':::::'    Memory: 142MB / 4096MB", 0xFF10B981); ly += 18;
        ws_draw_text(p, pitch, scr_w, scr_h, tx + 18, ly, "   ':'      Disk: 512MB /dev/disk0 (FAT32)", 0xFF10B981); ly += 22;

        ws_draw_text_bold(p, pitch, scr_w, scr_h, tx + 14, ly, "root@Mac ~ # ", 0xFF38BDF8);
        ws_draw_text(p, pitch, scr_w, scr_h, tx + 14 + 13*9, ly, "ls -la", 0xFFF1F5F9); ly += 18;
        ws_draw_text(p, pitch, scr_w, scr_h, tx + 14, ly, "Applications  Library  System  Users  bin  sbin", 0xFF94A3B8); ly += 20;

        ws_draw_text_bold(p, pitch, scr_w, scr_h, tx + 14, ly, "root@Mac ~ #", 0xFF38BDF8);
        ws_fill_rect(p, pitch, scr_w, scr_h, tx + 14 + 13*9, ly, 8, 16, 0xFF38BDF8); // cursor
    }

    // --- Calculator Window ---
    if (s_calc_open) {
        int cx = (int)s_calc_frame.origin.x;
        int cy = (int)s_calc_frame.origin.y;
        int cw = (int)s_calc_frame.size.width;
        int ch = (int)s_calc_frame.size.height;
        BOOL is_act = (s_active_window == 2);

        // Shadow
        ws_draw_rounded_rect(p, pitch, scr_w, scr_h, cx - 4, cy - 2, cw + 8, ch + 8, 10, 0x50000000, 0);
        // Titlebar
        ws_draw_rounded_rect(p, pitch, scr_w, scr_h, cx, cy, cw, 28, 8, is_act ? 0xFF282E3A : 0xFF1C212B, 0xFF3D4656);
        ws_draw_traffic_lights(p, pitch, scr_w, scr_h, cx + 8, cy + 8, -1);
        ws_draw_text_bold(p, pitch, scr_w, scr_h, cx + 70, cy + 6, "Calculator", 0xFFE2E8F0);

        // Interior
        ws_fill_rect(p, pitch, scr_w, scr_h, cx, cy + 28, cw, ch - 28, 0xFF1E2430);
        ws_draw_rounded_rect(p, pitch, scr_w, scr_h, cx, cy, cw, ch, 8, 0, 0xFF3D4656);

        // LCD Display
        ws_draw_rounded_rect(p, pitch, scr_w, scr_h, cx + 12, cy + 36, 216, 42, 6, 0xFF12161F, 0xFF333E50);
        int str_len = (int)strlen(s_calc_str);
        ws_draw_text_bold(p, pitch, scr_w, scr_h, cx + 215 - str_len * 9, cy + 48, s_calc_str, 0xFFFFFFFF);

        // 4x5 Button Grid
        const char *btn_labels[5][4] = {
            {"C", "+/-", "%", "/"},
            {"7", "8", "9", "*"},
            {"4", "5", "6", "-"},
            {"1", "2", "3", "+"},
            {"0", "", ".", "="}
        };

        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < 4; c++) {
                if (r == 4 && c == 1) continue; // 0 spans 2 cols
                int bx = cx + 12 + c * 54;
                int by = cy + 88 + r * 46;
                int bw = (r == 4 && c == 0) ? 102 : 48;
                int bh = 40;

                uint32_t bg_col = 0xFF2F3644;
                if (c == 3 || (r == 4 && c == 3)) bg_col = 0xFFFF9F0A; // orange operator
                else if (r == 0) bg_col = 0xFF4B5563; // slate top row

                ws_draw_rounded_rect(p, pitch, scr_w, scr_h, bx, by, bw, bh, 6, bg_col, 0x40FFFFFF);
                const char *lbl = btn_labels[r][c];
                int lbl_w = (int)strlen(lbl) * 8;
                ws_draw_text_bold(p, pitch, scr_w, scr_h, bx + (bw - lbl_w)/2, by + 12, lbl, 0xFFFFFFFF);
            }
        }
    }

    // --- About This Mac Window ---
    if (s_about_open) {
        int ax = (int)s_about_frame.origin.x;
        int ay = (int)s_about_frame.origin.y;
        int aw = (int)s_about_frame.size.width;
        int ah = (int)s_about_frame.size.height;
        BOOL is_act = (s_active_window == 3);

        ws_draw_rounded_rect(p, pitch, scr_w, scr_h, ax - 4, ay - 2, aw + 8, ah + 8, 10, 0x50000000, 0);
        ws_draw_rounded_rect(p, pitch, scr_w, scr_h, ax, ay, aw, 28, 8, is_act ? 0xFF282E3A : 0xFF1C212B, 0xFF3D4656);
        ws_draw_traffic_lights(p, pitch, scr_w, scr_h, ax + 8, ay + 8, -1);
        ws_draw_text_bold(p, pitch, scr_w, scr_h, ax + 120, ay + 6, "About This Mac", 0xFFE2E8F0);

        ws_fill_rect(p, pitch, scr_w, scr_h, ax, ay + 28, aw, ah - 28, 0xFF1A202C);
        ws_draw_rounded_rect(p, pitch, scr_w, scr_h, ax, ay, aw, ah, 8, 0, 0xFF3D4656);

        ws_draw_apple_logo(p, pitch, scr_w, scr_h, ax + 36, ay + 60, 0xFFFFFFFF);
        ws_draw_text_bold(p, pitch, scr_w, scr_h, ax + 60, ay + 60, "XIU OS Sonoma", 0xFFFFFFFF);
        ws_draw_text(p, pitch, scr_w, scr_h, ax + 60, ay + 82, "Version 1.0 (Darwin 24.0.0 XNU)", 0xFF94A3B8);
        ws_draw_text(p, pitch, scr_w, scr_h, ax + 60, ay + 104, "MacBook Pro (XIU Virtual Architecture)", 0xFFCBD5E1);
        ws_draw_text(p, pitch, scr_w, scr_h, ax + 60, ay + 126, "Processor: 4-Core Virtual Mac CPU", 0xFFCBD5E1);
        ws_draw_text(p, pitch, scr_w, scr_h, ax + 60, ay + 148, "Memory: 4096 MB RAM", 0xFFCBD5E1);
        ws_draw_text(p, pitch, scr_w, scr_h, ax + 60, ay + 170, "Graphics: BSDFramebuffer 1280x800", 0xFFCBD5E1);
    }

    // 5. Bottom Floating macOS Dock
    int dock_w = 380, dock_h = 58;
    int dock_x = (scr_w - dock_w) / 2;
    int dock_y = scr_h - 70;

    // Soft drop shadow
    ws_draw_rounded_rect(p, pitch, scr_w, scr_h, dock_x - 3, dock_y - 1, dock_w + 6, dock_h + 6, 18, 0x40000000, 0);
    // Frosted glass background
    ws_draw_rounded_rect(p, pitch, scr_w, scr_h, dock_x, dock_y, dock_w, dock_h, 16, 0xD01E2638, 0x60FFFFFF);

    // Icon 0: Finder (Cyan Mac face)
    int ix0 = dock_x + 14, iy0 = dock_y + 8;
    ws_draw_rounded_rect(p, pitch, scr_w, scr_h, ix0, iy0, 42, 42, 10, 0xFF00A8E8, 0x50FFFFFF);
    ws_draw_text_bold(p, pitch, scr_w, scr_h, ix0 + 13, iy0 + 12, "(:", 0xFFFFFFFF);
    ws_draw_circle(p, pitch, scr_w, scr_h, ix0 + 21, dock_y + 53, 2, 0xFF38BDF8, 0);

    // Icon 1: Launchpad (Cosmic grid)
    int ix1 = dock_x + 66, iy1 = dock_y + 8;
    ws_draw_rounded_rect(p, pitch, scr_w, scr_h, ix1, iy1, 42, 42, 10, 0xFF4338CA, 0x50FFFFFF);
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            ws_draw_circle(p, pitch, scr_w, scr_h, ix1 + 13 + c*8, iy1 + 13 + r*8, 2, 0xFFFFFFFF, 0);
        }
    }

    // Icon 2: Terminal (Dark slate console with >_)
    int ix2 = dock_x + 118, iy2 = dock_y + 8;
    ws_draw_rounded_rect(p, pitch, scr_w, scr_h, ix2, iy2, 42, 42, 10, 0xFF0F172A, 0x50FFFFFF);
    ws_draw_text_bold(p, pitch, scr_w, scr_h, ix2 + 11, iy2 + 12, ">_", 0xFF10B981);
    if (s_term_open) ws_draw_circle(p, pitch, scr_w, scr_h, ix2 + 21, dock_y + 53, 2, 0xFF38BDF8, 0);

    // Icon 3: Calculator (Orange arithmetic +-x=)
    int ix3 = dock_x + 170, iy3 = dock_y + 8;
    ws_draw_rounded_rect(p, pitch, scr_w, scr_h, ix3, iy3, 42, 42, 10, 0xFFFF9F0A, 0x50FFFFFF);
    ws_draw_text_bold(p, pitch, scr_w, scr_h, ix3 + 8, iy3 + 7, "+ -", 0xFFFFFFFF);
    ws_draw_text_bold(p, pitch, scr_w, scr_h, ix3 + 8, iy3 + 22, "x =", 0xFFFFFFFF);
    if (s_calc_open) ws_draw_circle(p, pitch, scr_w, scr_h, ix3 + 21, dock_y + 53, 2, 0xFF38BDF8, 0);

    // Icon 4: System Settings (Gear)
    int ix4 = dock_x + 222, iy4 = dock_y + 8;
    ws_draw_rounded_rect(p, pitch, scr_w, scr_h, ix4, iy4, 42, 42, 10, 0xFF64748B, 0x50FFFFFF);
    ws_draw_circle(p, pitch, scr_w, scr_h, ix4 + 21, iy4 + 21, 10, 0xFF94A3B8, 0xFF475569);
    ws_draw_circle(p, pitch, scr_w, scr_h, ix4 + 21, iy4 + 21, 4, 0xFF334155, 0);

    // Divider
    ws_fill_rect(p, pitch, scr_w, scr_h, dock_x + 272, dock_y + 12, 1, 34, 0x33FFFFFF);

    // Icon 5: Trash
    int ix5 = dock_x + 286, iy5 = dock_y + 8;
    ws_draw_rounded_rect(p, pitch, scr_w, scr_h, ix5, iy5, 42, 42, 10, 0xFF334155, 0x50FFFFFF);
    ws_draw_rounded_rect(p, pitch, scr_w, scr_h, ix5 + 11, iy5 + 11, 20, 22, 3, 0xFF94A3B8, 0xFFCBD5E1);
    ws_fill_rect(p, pitch, scr_w, scr_h, ix5 + 8, iy5 + 8, 26, 3, 0xFFCBD5E1);

    // Tooltip over hovered Dock icon
    int mx = (int)cursorPos.x, my = (int)cursorPos.y;
    if (my >= dock_y && my <= dock_y + dock_h) {
        const char *tip = NULL;
        int tip_x = 0;
        if (mx >= ix0 && mx < ix0 + 42) { tip = "Finder"; tip_x = ix0; }
        else if (mx >= ix1 && mx < ix1 + 42) { tip = "Launchpad"; tip_x = ix1 - 10; }
        else if (mx >= ix2 && mx < ix2 + 42) { tip = "Terminal"; tip_x = ix2 - 6; }
        else if (mx >= ix3 && mx < ix3 + 42) { tip = "Calculator"; tip_x = ix3 - 14; }
        else if (mx >= ix4 && mx < ix4 + 42) { tip = "Settings"; tip_x = ix4 - 6; }
        else if (mx >= ix5 && mx < ix5 + 42) { tip = "Trash"; tip_x = ix5; }

        if (tip) {
            int tw = (int)strlen(tip) * 8 + 16;
            ws_draw_rounded_rect(p, pitch, scr_w, scr_h, tip_x, dock_y - 28, tw, 22, 5, 0xEE1E2638, 0x60FFFFFF);
            ws_draw_text(p, pitch, scr_w, scr_h, tip_x + 8, dock_y - 24, tip, 0xFFFFFFFF);
        }
    }

    // 6. Draw macOS Cursor (Black arrow with 1px white outline)
    int cx = (int)cursorPos.x;
    int cy = (int)cursorPos.y - _cursor_height;
    for (int r = 0; r < 15; r++) {
        for (int c = 0; c <= r / 2 + 1; c++) {
            ws_put_pixel(p, pitch, scr_w, scr_h, cx + c, cy + r, 0xFF000000);
            if (c > 0 && c < r / 2 + 1 && r < 14) {
                ws_put_pixel(p, pitch, scr_w, scr_h, cx + c, cy + r, 0xFFFFFFFF);
            }
        }
    }
}

// Compositor Main Loop
-(void)run {
    int scr_w = [fb width];
    int scr_h = [fb height];
    int pitch = [fb stride] / 4;
    if (scr_w <= 0) scr_w = (int)_geometry.size.width;
    if (scr_h <= 0) scr_h = (int)_geometry.size.height;
    if (pitch <= 0) pitch = scr_w;

    uint32_t *raw_pixels = (uint32_t *)[fb pixels];
    uint32_t *vram_pixels = (uint32_t *)[fb vram];
    if (raw_pixels) {
        printf("[WindowServer] -run: Rendering initial desktop scene to pixels=%p, vram=%p (%dx%d, pitch=%d)...\n",
               raw_pixels, vram_pixels, scr_w, scr_h, pitch);
        ws_render_desktop(self, raw_pixels, pitch, scr_w, scr_h, NSMakePoint(scr_w / 2, scr_h / 2));
        if (vram_pixels && raw_pixels != vram_pixels) {
            memcpy(vram_pixels, raw_pixels, pitch * scr_h * 4);
        }
        [fb draw];
        printf("[WindowServer] -run: Initial desktop scene drawn successfully!\n");
        fflush(stdout);
    }

    // Launch Desktop Environment (SystemUIServer for MenuBar & Extras, Dock for application launcher)
    printf("[WindowServer] -run: Launching desktop session (SystemUIServer & Dock)...\n");
    fflush(stdout);

    pid_t sysui_pid = fork();
    if (sysui_pid == 0) {
        printf("[SystemUIServer child] Starting execl...\n");
        fflush(stdout);
        execl("/System/Library/CoreServices/SystemUIServer", "SystemUIServer", NULL);
        execl("/bin/SystemUIServer", "SystemUIServer", NULL);
        printf("[SystemUIServer child] execl failed: errno=%d\n", errno);
        fflush(stdout);
        _exit(1);
    }
    printf("[WindowServer] Forked SystemUIServer (PID %d)\n", sysui_pid);
    fflush(stdout);

    pid_t dock_pid = fork();
    if (dock_pid == 0) {
        printf("[Dock child] Starting execl...\n");
        fflush(stdout);
        execl("/System/Library/CoreServices/Dock", "Dock", NULL);
        execl("/bin/Dock", "Dock", NULL);
        printf("[Dock child] execl failed: errno=%d\n", errno);
        fflush(stdout);
        _exit(1);
    }
    printf("[WindowServer] Forked Dock (PID %d)\n", dock_pid);
    fflush(stdout);

    NSRect cursorRect = NSMakeRect(0, 0, _cursor_height, _cursor_height);

    struct pollfd fds;
    fds.fd = [input fileDescriptor];
    fds.events = POLLIN;

    uint64_t frame_count = 0;
    printf("[WindowServer] Compositor loop started: geometry=%dx%d, pitch=%d, ctxPixels=%p, vram=%p (inputFD=%d)\n",
           scr_w, scr_h, pitch, [fb pixels], [fb vram], fds.fd);
    fflush(stdout);

    BOOL needsRedraw = YES;
    NSPoint lastCursorPos = NSMakePoint(-1, -1);

    while(ready == YES) {
        // 1. Drain incoming Mach messages
        for(int m = 0; m < 16; ++m) {
            if([self receiveMachMessage]) {
                needsRedraw = YES;
            }
        }

        // 2. Poll input events (mouse / keyboard)
        if(fds.fd >= 0 && poll(&fds, 1, 10) > 0) {
            [input run:self];
            needsRedraw = YES;
        }

        cursorRect.origin = [input pointerPos];
        if(cursorRect.origin.x != lastCursorPos.x || cursorRect.origin.y != lastCursorPos.y) {
            lastCursorPos = cursorRect.origin;
            needsRedraw = YES;
        }

        // 3. Render desktop background, windows, decorations
        frame_count++;
        if(needsRedraw || (frame_count % 30) == 0) {
            needsRedraw = NO;
            pthread_mutex_lock(&renderLock);
            
            uint32_t *raw_pixels = (uint32_t *)[fb pixels];
            uint32_t *vram_pixels = (uint32_t *)[fb vram];

            if (raw_pixels) {
                // Render rich desktop UI (wallpaper, 24px frosted MenuBar, bottom floating glass Dock, built-in apps, cursor)
                ws_render_desktop(self, raw_pixels, pitch, scr_w, scr_h, cursorRect.origin);

                // Render client windows if any
                O2BitmapContext *ctx = [fb context];
                if (ctx) {
                    for(int level = 0; level < kCGNumReservedWindowLevels; ++level) {
                        NSArray *wins = _windows[level];
                        int count = [wins count];
                        for(int i = 0; i < count; ++i) {
                            WSWindowRecord *win = [wins objectAtIndex:i];
                            if(win.state == HIDDEN || win.state == MINIMIZED || win.state == CLOSED)
                                continue;
                            if(win != curWindow && win.surface) {
                                [ctx drawImage:win.surface inRect:win.geometry];
                            }
                        }
                        if(curWindow && curWindow.level == level && curWindow.state != MINIMIZED
                                && curWindow.state != HIDDEN && curWindow.surface) {
                            [ctx drawImage:curWindow.surface inRect:curWindow.geometry];
                        }
                    }
                }

                // If direct VRAM pointer is available, ensure instant blit
                if (vram_pixels && raw_pixels != vram_pixels) {
                    memcpy(vram_pixels, raw_pixels, pitch * scr_h * 4);
                }

                [fb draw];

                if(frame_count == 1 || (frame_count % 60) == 0) {
                    printf("[WindowServer] Compositor frame #%llu rendered (scr=%dx%d, raw_pixels=%p, vram=%p, wins=%lu)\n",
                           frame_count, scr_w, scr_h, raw_pixels, vram_pixels, (unsigned long)[_windows[kCGNormalWindowLevelKey] count]);
                    fflush(stdout);
                }
            }
            pthread_mutex_unlock(&renderLock);
        }
    }
}

- (void)rpcMainDisplayID:(PortMessage *)msg {
    uint32_t ID = [(WSDisplay *)fb getDisplayID];
    struct wsRPCSimple reply = { kCGMainDisplayID, sizeof(uint32_t)*4, ID, 0, 0, 0 };
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

- (void)rpcGetOnlineDisplayList:(PortMessage *)msg {
    size_t size = sizeof(struct wsRPCBase) + sizeof(uint32_t)*[displays count];
    uint8_t *list = malloc(size);
    struct wsRPCBase *p = (struct wsRPCBase *)list;
    p->code = kCGGetOnlineDisplayList;
    p->len = 0;
    uint32_t *q = (uint32_t *)(list + sizeof(struct wsRPCBase));
    int j = 0;
    for(int i = 0; i < [displays count]; ++i) {
        WSDisplay *d = [displays objectAtIndex:i];
        if([d isOnline])
            q[j++] = [d getDisplayID];
    }
    p->len = j * sizeof(uint32_t);
    [self sendInlineData:list length:size withCode:MSG_ID_RPC toPort:msg->descriptor.name];
    free(list);
}

- (void)rpcGetActiveDisplayList:(PortMessage *)msg {
    size_t size = sizeof(struct wsRPCBase) + sizeof(uint32_t)*[displays count];
    uint8_t *list = malloc(size);
    struct wsRPCBase *p = (struct wsRPCBase *)list;
    p->code = kCGGetActiveDisplayList;
    p->len = 0;
    uint32_t *q = (uint32_t *)(list + sizeof(struct wsRPCBase));
    int j = 0;
    for(int i = 0; i < [displays count]; ++i) {
        WSDisplay *d = [displays objectAtIndex:i];
        if([d isActive])
            q[j++] = [d getDisplayID];
    }
    p->len = j * sizeof(uint32_t);
    [self sendInlineData:list length:size withCode:MSG_ID_RPC toPort:msg->descriptor.name];
    free(list);
}

- (void)rpcGetDisplaysWithOpenGLDisplayMask:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    CGOpenGLDisplayMask mask = args->val1;

    size_t size = sizeof(struct wsRPCBase) + sizeof(uint32_t)*[displays count];
    uint8_t *list = malloc(size);
    struct wsRPCBase *p = (struct wsRPCBase *)list;
    p->code = kCGGetDisplaysWithOpenGLDisplayMask;
    p->len = 0;
    uint32_t *q = (uint32_t *)(list + sizeof(struct wsRPCBase));
    int j = 0;
    for(int i = 0; i < [displays count]; ++i) {
        WSDisplay *d = [displays objectAtIndex:i];
        if([d openGLMask] & mask)
            q[j++] = [d getDisplayID];
    }
    p->len = j * sizeof(uint32_t);
    [self sendInlineData:list length:size withCode:MSG_ID_RPC toPort:msg->descriptor.name];
    free(list);
}

- (void)rpcGetDisplaysWithPoint:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    NSPoint point = NSMakePoint(args->val1, args->val2);

    size_t size = sizeof(struct wsRPCBase) + sizeof(uint32_t)*[displays count];
    uint8_t *list = malloc(size);
    struct wsRPCBase *p = (struct wsRPCBase *)list;
    p->code = kCGGetDisplaysWithPoint;
    p->len = 0;
    uint32_t *q = (uint32_t *)(list + sizeof(struct wsRPCBase));
    int j = 0;
    for(int i = 0; i < [displays count]; ++i) {
        WSDisplay *d = [displays objectAtIndex:i];
        if(NSPointInRect(point, [d geometry])) // FIXME: this should refer to global coordinates
            q[j++] = [d getDisplayID];
    }
    p->len = j * sizeof(uint32_t);
    [self sendInlineData:list length:size withCode:MSG_ID_RPC toPort:msg->descriptor.name];
    free(list);
}

- (void)rpcGetDisplaysWithRect:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    NSRect rect = NSMakeRect(args->val1, args->val2, args->val3, args->val4);

    size_t size = sizeof(struct wsRPCBase) + sizeof(uint32_t)*[displays count];
    uint8_t *list = malloc(size);
    struct wsRPCBase *p = (struct wsRPCBase *)list;
    p->code = kCGGetDisplaysWithRect;
    p->len = 0;
    uint32_t *q = (uint32_t *)(list + sizeof(struct wsRPCBase));
    int j = 0;
    for(int i = 0; i < [displays count]; ++i) {
        WSDisplay *d = [displays objectAtIndex:i];
        if(NSIntersectsRect([d geometry], rect)) // FIXME: this should refer to global coordinates
            q[j++] = [d getDisplayID];
    }
    p->len = j * sizeof(uint32_t);
    [self sendInlineData:list length:size withCode:MSG_ID_RPC toPort:msg->descriptor.name];
    free(list);
}

- (void)rpcOpenGLDisplayMaskToDisplayID:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    CGOpenGLDisplayMask mask = args->val1;

    struct wsRPCSimple reply = {0};
    reply.base.code = kCGOpenGLDisplayMaskToDisplayID;
    reply.val1 = kCGNullDirectDisplay;
    reply.base.len = 4;

    for(int i = 0; i < [displays count]; ++i) {
        WSDisplay *d = [displays objectAtIndex:i];
        if([d openGLMask] & mask)
            reply.val1 = [d getDisplayID];
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

- (void)rpcDisplayIDToOpenGLDisplayMask:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    CGDirectDisplayID ID = args->val1;

    struct wsRPCSimple reply = {0};
    reply.base.code = kCGDisplayIDToOpenGLDisplayMask;
    reply.base.len = 4;

    for(int i = 0; i < [displays count]; ++i) {
        WSDisplay *d = [displays objectAtIndex:i];
        if([d getDisplayID] == ID)
            reply.val1 = [d openGLMask];
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

- (void)rpcDisplayCapture:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    WSDisplay *display = [self displayWithID:args->val1];
    struct wsRPCSimple reply = { {kCGDisplayCaptureWithOptions, 4}, kCGErrorSuccess };
    if(!display || [display capture:msg->pid withOptions:args->val2] != YES)
        reply.val1 = kCGErrorFailure;
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

- (void)rpcDisplayRelease:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    WSDisplay *display = [self displayWithID:args->val1];
    struct wsRPCSimple reply = { {kCGDisplayRelease, 4}, kCGErrorSuccess };
    if(!display)
        reply.val1 = kCGErrorFailure;
    else
        [display releaseCapture];
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

- (void)rpcCaptureAllDisplays:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    WSDisplay *display = nil;
    struct wsRPCSimple reply = { {kCGCaptureAllDisplaysWithOptions, 4}, kCGErrorSuccess };
    for(int i = 0; i < [displays count]; ++i) {
        display = [displays objectAtIndex:i];
        if([display capture:msg->pid withOptions:args->val1] != YES) {
            [displays makeObjectsPerformSelector:@selector(releaseCapture)];
            reply.val1 = kCGErrorFailure;
            break;
        }
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

- (void)rpcReleaseAllDisplays:(PortMessage *)msg {
    [displays makeObjectsPerformSelector:@selector(releaseCapture)];
    struct wsRPCSimple reply = { {kCGReleaseAllDisplays, 4}, kCGErrorSuccess };
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

/* Conceptually, this function returns a pointer to the graphics context
 * owned by WindowServer, that the client app can then write to. Apple
 * probably does this with Mach shared memory, which we don't have. We do
 * it with SysV SHM segments
 */
- (void)rpcDisplayGetDrawingContext:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplayGetDrawingContext, 4}, 0};
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        reply.val1 = [display getCapturedContextID];
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

// FIXME: I think this is supposed to be only for apps that have captured the display?
- (void)rpcDisplayCreateImageForRect:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplayCreateImageForRect, 4}, 0 };
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        O2Rect rect;
        rect.origin.x = (args->val2 & 0xFFFF0000) >> 16;
        rect.origin.y = (args->val2 & 0xFFFF);
        rect.size.width = (args->val3 & 0xFFFF0000) >> 16;
        rect.size.height = (args->val3 & 0xFFFF);
        O2ImageRef img = [display imageForRect:rect];
        int imglen = 0;
        if(img) {
            imglen = O2ImageGetBytesPerRow(img) * O2ImageGetHeight(img);
            reply.val1 = shmget(args->val1 ^ random(), imglen, IPC_CREAT|0666);
        }
        if(reply.val1) {
            uint8_t *p = shmat(reply.val1, NULL, 0);
            if(!p)
                shmctl(reply.val1, IPC_RMID, NULL);
            else {
                memcpy(p, [img directBytes], imglen);
                shmdt(p);
            }
        }
        if(img)
            O2ImageRelease(img);
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

// Configuring Displays
-(void)rpcCompleteDisplayConfiguration:(PortMessage *)msg {
    struct _CGDisplayConfigInner *inner = (struct _CGDisplayConfigInner *)msg->data;
    struct wsRPCSimple reply = { {kCGCompleteDisplayConfiguration, 4}, 0 };

    if(logLevel >= WS_INFO)
        NSLog(@"configuring displays. rpc={%u %u}, length %u option %u", 
                inner->rpc.code, inner->rpc.len, inner->length, inner->option);
    int i = sizeof(struct _CGDisplayConfigInner);

    CGError ret = kCGErrorSuccess; // let's be optimistic
 
    while(i < inner->length) {
        char *p = (char *)inner + i;
        int opcode = *(uint32_t *)p;
        switch(opcode) {
            case CGDISPCFG_MIRROR: {
                struct _CGDispCfgMirror *op = p;
                if(logLevel >= WS_INFO)
                    NSLog(@"mirroring: %x %x", op->display, op->primary);
                WSDisplay *display = [self displayWithID:op->display];
                WSDisplay *primary = [self displayWithID:op->primary];
                i += sizeof(struct _CGDispCfgMirror);
                if(display == nil || primary == nil) {
                    ret = kCGErrorIllegalArgument;
                    break;
                }
                if([display mirror:primary] == NO)
                    ret = kCGErrorFailure;
                break;
            }
            case CGDISPCFG_ORIGIN: {
                struct _CGDispCfgOrigin *op = p;
                if(logLevel >= WS_INFO)
                    NSLog(@"set origin: %x %d,%d", op->display, op->x, op->y);
                i += sizeof(struct _CGDispCfgOrigin);
                WSDisplay *display = [self displayWithID:op->display];
                if(display == nil) {
                    ret = kCGErrorIllegalArgument;
                    break;
                }
                if([display setOriginX:op->x Y:op->y] == NO)
                    ret = kCGErrorFailure;
                break;
            }
            case CGDISPCFG_MODE: {
                struct _CGDispCfgMode *op = p;
                if(logLevel >= WS_INFO)
                    NSLog(@"set mode: %x %u %u %.2f %08x", op->display, op->mode.width,
                            op->mode.height, op->mode.refresh, op->mode.flags);
                WSDisplay *display = [self displayWithID:op->display];
                i += sizeof(struct _CGDispCfgMode);
                if(display == nil) {
                    ret = kCGErrorIllegalArgument;
                    break;
                }
                if([display setMode:&op->mode] == NO)
                    ret = kCGErrorFailure;
                break;
            }
            default:
                 NSLog(@"Unknown display configuration command");
                 i = inner->length;
                 break;
        };

        if(ret != kCGErrorSuccess)
            break;
    }

    if(ret == kCGErrorSuccess) {
        [displays makeObjectsPerformSelector:
            inner->option == kCGConfigureForAppOnly
                ? @selector(saveAppConfig)
                : inner->option == kCGConfigureForSession
                    ? @selector(saveSessionConfig)
                    : @selector(savePermanentConfig)
            withObject:nil];
    } else {
        // FIXME: roll back any changes to our saved config if something failed
    }

    reply.val1 = ret;
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcRestorePermanentDisplayConfiguration:(PortMessage *)msg {
    [displays makeObjectsPerformSelector:@selector(restorePermanentConfig) withObject:nil];
}

// Getting the Display Configuration
-(void)rpcDisplayCopyColorSpace:(PortMessage *)msg {
    // FIXME: not implemented yet
}

-(void)rpcDisplayStateFlags:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplayStateFlags, 4}, 0};
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        reply.val1 = [display flags];
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplayMirrorsDisplay:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplayMirrorsDisplay, 4}, 0};
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        WSDisplay *primary = [display mirrorOf];
        if(primary)
            reply.val1 = [primary getDisplayID];
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplayModelNumber:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplayModelNumber, 4}, 0};
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        reply.val1 = [display modelNumber];
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplayPrimaryDisplay:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplayPrimaryDisplay, 4}, args->val1};
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        WSDisplay *primary = [display primaryDisplay];
        if(primary)
            reply.val1 = [primary getDisplayID];
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplayRotation:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplayRotation, 4}, 0};
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        reply.val1 = (uint32_t)[display rotation];
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplayScreenSize:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplayScreenSize, 8}, 0};
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        CGSize size = [display screenSizeMM];
        reply.val1 = (uint32_t)size.width;
        reply.val2 = (uint32_t)size.height;
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplaySerialNumber:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplaySerialNumber, 4}, 0};
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        reply.val1 = [display serialNumber];
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplayUnitNumber:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplayUnitNumber, 4}, 0};
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        reply.val1 = [displays indexOfObject:display];
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplayVendorNumber:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplayVendorNumber, 4}, 0};
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        reply.val1 = [display vendorNumber];
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

// Retrieving Display Parameters
-(void)rpcDisplayBounds:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplayBounds, 16}, 0, 0, 0, 0 };
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        CGRect rect = [display geometry];
        reply.val1 = rect.origin.x;
        reply.val2 = rect.origin.y;
        reply.val3 = rect.size.width;
        reply.val4 = rect.size.height;
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

// Creating and Managing Display Modes
-(void)rpcDisplayCopyDisplayMode:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct {
        struct wsRPCBase base;
        struct CGDisplayMode mode;
    } reply;
    memset(&reply, 0, sizeof(reply));
    reply.base.code = kCGDisplayCopyDisplayMode;
    reply.base.len = 0;
    
    WSDisplay *display = [self displayWithID:args->val1];
    printf("[WindowServer] rpcDisplayCopyDisplayMode: displayID=0x%x display=%p\n", args->val1, display);
    fflush(stdout);
    if(display) {
        reply.base.len = sizeof(struct CGDisplayMode);
        struct CGDisplayMode *mode = [display currentMode];
        if(mode) {
            printf("[WindowServer] rpcDisplayCopyDisplayMode: mode=(%ux%u)\n", mode->width, mode->height);
            fflush(stdout);
            memcpy(&reply.mode, mode, sizeof(struct CGDisplayMode));
        }
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplayCopyAllDisplayModes:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct {
        struct wsRPCBase base;
        struct CGDisplayMode mode[32];
    } reply;
    reply.base.code = kCGDisplayCopyAllDisplayModes;
    reply.base.len = 0;
    
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        CFArrayRef allModes = [display allModes];
        for(int i = 0; i < CFArrayGetCount(allModes); ++i) {
            memcpy(&reply.mode[i], CFArrayGetValueAtIndex(allModes, i), sizeof(struct CGDisplayMode));
            reply.base.len += sizeof(struct CGDisplayMode);
        }
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplaySetDisplayMode:(PortMessage *)msg {
    struct wsRPCSimple *args = (struct wsRPCSimple *)msg->data;
    struct wsRPCSimple reply = { {kCGDisplaySetDisplayMode, 4}, kCGErrorIllegalArgument, 0, 0, 0 };
    
    WSDisplay *display = [self displayWithID:args->val1];
    if(display) {
        struct CGDisplayMode *mode = &(args->val2);
        if([display setMode:mode])
            reply.val1 = kCGErrorSuccess;
        else
            reply.val1 = kCGErrorFailure;
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

// Adjusting Display Gamma
-(void)rpcSetDisplayTransferByFormula:(PortMessage *)msg {
    struct {
        struct wsRPCBase base;
        uint32_t display;
        CGGammaValue vals[9];
    } *data = msg->data;

    int ret = kCGErrorIllegalArgument;
    WSDisplay *display = [self displayWithID:data->display];
    int count = 0;
    if(display)
        count = [display gammaTableSize];

    float *red, *green, *blue;
    float redMin, redMax, redGamma;
    float greenMin, greenMax, greenGamma;
    float blueMin, blueMax, blueGamma;

    if(count > 0) {
        redMin = data->vals[0];
        redMax = data->vals[1];
        redGamma = data->vals[2];
        greenMin = data->vals[3];
        greenMax = data->vals[4];
        greenGamma = data->vals[5];
        blueMin = data->vals[6];
        blueMax = data->vals[7];
        blueGamma = data->vals[8];

        red = malloc(sizeof(float)*count);
        green = malloc(sizeof(float)*count);
        blue = malloc(sizeof(float)*count);
    }

    ret = kCGErrorFailure;
    if(red && green && blue && count > 2) {
        red[0] = redMin + ((redMax - redMin) * pow(0, redGamma)); 
        green[0] = greenMin + ((greenMax - greenMin) * pow(0.0, greenGamma)); 
        blue[0] = blueMin + ((blueMax - blueMin) * pow(0, blueGamma)); 

        float increment = 1.0/(float)(count - 2);
        int j = 1;
        for(float i = increment; j < count && i < 1.0; i += increment) {
            red[j] = redMin + ((redMax - redMin) * pow(i, redGamma));
            green[j] = greenMin + ((greenMax - greenMin) * pow(i, greenGamma));
            blue[j] = blueMin + ((blueMax - blueMin) * pow(i, blueGamma));
            ++j;
        }

        red[count] = redMin + ((redMax - redMin) * pow(1.0, redGamma)); 
        green[count] = greenMin + ((greenMax - greenMin) * pow(1.0, greenGamma)); 
        blue[count] = blueMin + ((blueMax - blueMin) * pow(1.0, blueGamma)); 
        
        ret = [display loadGammaTable:red green:green blue:blue] ? kCGErrorSuccess : kCGErrorFailure;

        free(red);
        free(green);
        free(blue);
    }

    struct wsRPCSimple reply = { {kCGSetDisplayTransferByFormula, 4}, 0, 0, 0, 0 };
    reply.val1 = ret;
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcGetDisplayTransferByFormula:(PortMessage *)msg {
    struct {
        struct wsRPCBase base;
        uint32_t display;
        CGGammaValue vals[9];
    } *data = msg->data;

    int ret = kCGErrorIllegalArgument;
    WSDisplay *display = [self displayWithID:data->display];
    int count = 0;
    if(display)
        count = [display gammaTableSize];

    float *red, *green, *blue;
    if(count > 0) {
        red = malloc(sizeof(float)*count);
        green = malloc(sizeof(float)*count);
        blue = malloc(sizeof(float)*count);
    }

    ret = kCGErrorFailure;
    data->vals[0] = 1; // redMin
    data->vals[1] = 0; // redMax
    data->vals[3] = 1; // greenMin
    data->vals[4] = 0; // greenMax
    data->vals[6] = 1; // blueMin
    data->vals[7] = 0; // blueMax

    float gamma;
    if(red && green && blue && count > 0) {
        for(int i = 0; i < count; ++i) {
            gamma = red[i];
            if(gamma < data->vals[0])
                data->vals[0] = gamma; // new minimum
            if(gamma > data->vals[1])
                data->vals[1] = gamma; // new maximum

            gamma = green[i];
            if(gamma < data->vals[3])
                data->vals[0] = gamma; // new minimum
            if(gamma > data->vals[4])
                data->vals[1] = gamma; // new maximum

            gamma = blue[i];
            if(gamma < data->vals[6])
                data->vals[0] = gamma; // new minimum
            if(gamma > data->vals[7])
                data->vals[1] = gamma; // new maximum
        }

        free(red);
        free(green);
        free(blue);

        [display getGammaCoefficientRed:&data->vals[2] green:&data->vals[5] blue:&data->vals[8]];
    }

    data->base.len = 3 * count  * sizeof(CGGammaValue) + 4;
    data->display = ret;
    [self sendInlineData:msg->data length:sizeof(msg->data) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcSetDisplayTransferByTable:(PortMessage *)msg {
    struct wsRPCSimple *args = msg->data;
    int len = args->base.len - 4;
    int entries = len / (3*sizeof(CGGammaValue));

    int ret = kCGErrorIllegalArgument;
    WSDisplay *display = [self displayWithID:args->val1];

    if(display) {
        ret = kCGErrorFailure;
        int count = [display gammaTableSize];

        float *red = malloc(sizeof(float)*count);
        float *green = malloc(sizeof(float)*count);
        float *blue = malloc(sizeof(float)*count);

        float *p = &(args->val2);
        for(int i = 0; i < count && i < entries; ++i) {
            float r = *p++;
            float g = *p++;
            float b = *p++;
            if(red)
                red[i] = r;
            if(green)
                green[i] = g;
            if(blue)
                blue[i] = b;
        }
        if(red && green && blue)
            ret = [display loadGammaTable:red green:green blue:blue] ? kCGErrorSuccess : kCGErrorFailure;
    }

    struct wsRPCSimple reply = { {kCGSetDisplayTransferByTable, 4}, 0, 0, 0, 0 };
    reply.val1 = ret;
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcGetDisplayTransferByTable:(PortMessage *)msg {
    struct wsRPCSimple *args = msg->data;
    WSDisplay *display = [self displayWithID:args->val1];

    struct { CGGammaValue r; CGGammaValue g; CGGammaValue b; } *vals = &(args->val1);
    if(display) {
        int count = [display gammaTableSize];

        float *red = malloc(sizeof(float)*count);
        float *green = malloc(sizeof(float)*count);
        float *blue = malloc(sizeof(float)*count);
        if(red && green && blue && count > 0) {
            [display getGammaTablesWithCapacity:count red:red green:green blue:blue];

            int i = 0;
            while(vals < (msg->data + sizeof(msg->data))) {
                vals->r = red[i];
                vals->g = green[i];
                vals->b = blue[i];
                vals += sizeof(CGGammaValue) * 3;
                ++i;
            }
        }

        free(red);
        free(green);
        free(blue);
    }
    args->base.len = (uint8_t *)vals - msg->data;
    [self sendInlineData:msg->data length:sizeof(msg->data) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcSetDisplayTransferByByteTable:(PortMessage *)msg {
    struct wsRPCSimple *args = msg->data;
    int len = args->base.len - 4;
    int entries = len / 3;

    int ret = kCGErrorIllegalArgument;
    WSDisplay *display = [self displayWithID:args->val1];

    if(display) {
        ret = kCGErrorFailure;
        int count = [display gammaTableSize];

        uint8_t *red = malloc(count);
        uint8_t *green = malloc(count);
        uint8_t *blue = malloc(count);

        float *p = &(args->val2);
        for(int i = 0; i < count && i < entries; ++i) {
            uint8_t r = *p++;
            uint8_t g = *p++;
            uint8_t b = *p++;
            if(red)
                red[i] = r;
            if(green)
                green[i] = g;
            if(blue)
                blue[i] = b;
        }
        if(red && green && blue)
            ret = [display load8BitGammaTable:red green:green blue:blue] ? kCGErrorSuccess : kCGErrorFailure;
    }

    struct wsRPCSimple reply = { {kCGSetDisplayTransferByByteTable, 4}, 0, 0, 0, 0 };
    reply.val1 = ret;
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplayRestoreColorSyncSettings:(PortMessage *)msg {
    [displays makeObjectsPerformSelector:@selector(loadDefaultGamma) withObject:nil];
}

-(void)rpcDisplayGammaTableCapacity:(PortMessage *)msg {
    struct wsRPCSimple *args = msg->data;
    int ret = 0;
    WSDisplay *display = [self displayWithID:args->val1];
    struct wsRPCSimple reply = { {kCGDisplayGammaTableCapacity, 4}, 0, 0, 0, 0 };
    reply.val1 = display ? [display gammaTableSize] : 0;
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

// Controlling the Mouse Cursor
-(void)rpcDisplayHideCursor:(PortMessage *)msg {
    int ret = kCGErrorCannotComplete;
    if(msg->pid == [curApp pid]) {
        cursorHideCount++;
        ret = kCGErrorSuccess;
    }
    struct wsRPCSimple data;
    data.base.code = kCGDisplayHideCursor;
    data.base.len = 4;
    data.val1 = ret;
    [self sendInlineData:&data length:sizeof(data) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplayShowCursor:(PortMessage *)msg {
    int ret = kCGErrorCannotComplete;
    if(msg->pid == [curApp pid]) {
        cursorHideCount--;
        ret = kCGErrorSuccess;
    }
    struct wsRPCSimple data;
    data.base.code = kCGDisplayShowCursor;
    data.base.len = 4;
    data.val1 = ret;
    [self sendInlineData:&data length:sizeof(data) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcDisplayMoveCursorToPoint:(PortMessage *)msg {
    struct wsRPCSimple *data = msg->data;
    int ret = kCGErrorIllegalArgument;
    WSDisplay *display = [self displayWithID:data->val1];

    if(display) {
        ret = kCGErrorSuccess;
        CGRect bounds = [display geometry];
        double x = clipTo(data->val2, bounds.origin.x, bounds.size.width);
        double y = clipTo(data->val3, bounds.origin.y, bounds.size.height);
        [input setPointerPos:NSMakePoint(x, y)];
    }
    struct wsRPCSimple reply;
    reply.base.code = kCGDisplayMoveCursorToPoint;
    reply.base.len = 4;
    reply.val1 = ret;
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(WSAppRecord *)appForMessage:(PortMessage *)msg {
    if(!msg) return nil;
    if(msg->bundleID[0] != '\0') {
        WSAppRecord *app = [apps objectForKey:[NSString stringWithCString:msg->bundleID]];
        if(app) return app;
    }
    for(NSString *key in [apps allKeys]) {
        WSAppRecord *app = [apps objectForKey:key];
        if(app && app.pid == msg->pid)
            return app;
    }
    if(msg->pid == 3) return [apps objectForKey:@"com.ravynos.SystemUIServer"];
    if(msg->pid == 4 || msg->pid == 5) return [apps objectForKey:@"com.ravynos.Dock"];
    return nil;
}

-(void)rpcAssociateMouseAndMouseCursorPosition:(PortMessage *)msg {
    struct wsRPCSimple data;
    int ret = kCGErrorFailure;

    WSAppRecord *app = [self appForMessage:msg];
    if(app) {
        ret = kCGErrorSuccess;
        [app mouseCursorConnected:((struct wsRPCSimple *)msg->data)->val1];
    }
    data.base.code = kCGAssociateMouseAndMouseCursorPosition;
    data.base.len = 4;
    data.val1 = ret;
    [self sendInlineData:&data length:sizeof(data) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

// FIXME: this should be in Global Coordinate Space (all displays)
-(void)rpcWarpMouseCursorPosition:(PortMessage *)msg {
    struct wsRPCSimple *data = msg->data;
    int ret = kCGErrorIllegalArgument;
    WSDisplay *display = [self displayWithID:data->val1];

    if(display) {
        ret = kCGErrorSuccess;
        CGRect bounds = [display geometry];
        double x = clipTo(data->val2, bounds.origin.x, bounds.size.width);
        double y = clipTo(data->val3, bounds.origin.y, bounds.size.height);
        [input setPointerPos:NSMakePoint(x, y)];
    }

    struct wsRPCSimple reply;
    reply.base.code = kCGWarpMouseCursorPosition;
    reply.base.len = 4;
    reply.val1 = ret;
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcGetLastMouseDelta:(PortMessage *)msg {
    struct wsRPCSimple data;
    NSPoint pos = [input pointerPos];
    data.base.code = kCGGetLastMouseDelta;
    data.base.len = 8;
    data.val1 = pos.x;
    data.val2 = pos.y;
    [self sendInlineData:&data length:sizeof(data) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcWindowCreate:(PortMessage *)msg {
    struct wsRPCSimple reply = { {kWSWindowCreate, 4}, kWSErrorFailure, 0, 0, 0 };

    if(msg->len == sizeof(struct wsRPCWindow)) {
        struct wsRPCWindow *data = (struct wsRPCWindow*)msg->data;
        WSAppRecord *app = [self appForMessage:msg];

        if(app != nil) {
            if([self windowCreate:data forApp:app] != 0)
                reply.val1 = kWSErrorSuccess;
        } else {
            NSLog(@"No matching app for rpcWindowCreate! %s %u", msg->bundleID, msg->pid);
        }
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcWindowModifyState:(PortMessage *)msg {
    struct wsRPCSimple reply = { {kWSWindowModifyState, 4}, kWSErrorFailure, 0, 0, 0 };

    if(msg->len == sizeof(struct wsRPCWindow)) {
        struct wsRPCWindow *data = (struct wsRPCWindow*)msg->data;
        WSAppRecord *app = [self appForMessage:msg];

        if(app != nil) {
            [self windowModify:data forApp:app];
            reply.val1 = kWSErrorSuccess;
        } else {
            NSLog(@"No matching app for rpcWindowModifyState! %s %u", msg->bundleID, msg->pid);
        }
    }
    [self sendInlineData:&reply length:sizeof(reply) withCode:MSG_ID_RPC toPort:msg->descriptor.name];
}

-(void)rpcWindowDestroy:(PortMessage *)msg {
    if(msg->len == sizeof(struct wsRPCWindow)) {
        struct wsRPCWindow *data = (struct wsRPCWindow*)msg->data;
        WSAppRecord *app = [self appForMessage:msg];

        if(app != nil) {
            WSWindowRecord *winrec = [app removeWindowWithID:data->windowID];
            [self removeWindowFromAllLevels:winrec];
        } else {
            NSLog(@"No matching app for rpcWindowDestroy! %s %u", msg->bundleID, msg->pid);
        }
    }
}

/* App management. If a window number is included (and found), make it the active
 * window. If not, just activate the app and let it choose a window. Dock calls this
 * with window = 0 to activate a running app, and with a windowID to restore a
 * miniaturized window.
 */
-(void)rpcApplicationActivate:(PortMessage *)msg {
    struct wsRPCWindow *data = (struct wsRPCWindow *)msg->data;
    const char *bundleID = (const char *)((msg->data)+sizeof(struct wsRPCWindow));
    WSAppRecord *app = [apps objectForKey:[NSString stringWithCString:bundleID]];

    if(app != nil) {
        WSAppRecord *oldApp = curApp;
        WSWindowRecord *winrec = [app windowWithID:data->windowID];
        if(winrec != nil)
            winrec.state = winrec.prevState; // deminiaturize to previous state 
        else {
            // Dock has called this to activate an app. Check for minimized windows.
            NSArray *wins = [app windows];
            WSWindowRecord *restore = nil;
            BOOL visWindows = NO;
            for(int x = 0; x < [wins count]; ++x) {
                winrec = [wins objectAtIndex:x];
                if(winrec.state != MINIMIZED && winrec.state != HIDDEN && winrec.state != CLOSED) {
                    visWindows = YES;
                    break;
                } else if(winrec.state == MINIMIZED)
                    restore = winrec;
            }
            if(!visWindows) {
                winrec = nil;
                if(restore != nil) {
                    // No visible windows but something is minimized - restore it
                    // Otherwise we activate the app and let it choose one
                    restore.state = restore.prevState;
                    winrec = restore;
                }
            }
        }

        curApp = app;
        [self switchFromApp:oldApp toWindow:winrec];

        // now tell Dock and the app about the window state changes
        if(winrec != nil) {
            data->windowID = winrec.number;
            data->state = winrec.state;
            [self updateClientWindowState:winrec];
        }
        [self notifyDock:data length:sizeof(struct wsRPCWindow) 
                withCode:CODE_WINDOW_STATE forApp:app];
    } else {
        NSLog(@"No matching app for rpcApplicationActivate! %s", msg->bundleID);
    }
}

- (BOOL)receiveMachMessage {
    static ReceiveMessage msg;
    mach_msg_return_t result = mach_msg((mach_msg_header_t *)&msg, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(msg),
        _servicePort, 0, MACH_PORT_NULL);
    if(result != MACH_MSG_SUCCESS)
        return NO;
    else {
        switch(msg.msg.header.msgh_id) {
            case 5001 /* WS_MSG_CREATE_WINDOW */: {
                ws_req_create_t *req = (ws_req_create_t *)&msg;
                ws_rep_create_t rep;
                memset(&rep, 0, sizeof(rep));
                rep.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_MAKE_SEND, 0, 0, 0);
                rep.msgh_size = sizeof(rep);
                rep.msgh_remote_port = req->msgh_local_port;
                rep.msgh_id = 5001;

                NSRect frame = NSMakeRect(80, 80, req->width > 0 ? req->width : 360, req->height > 0 ? req->height : 240);
                WSWindowRecord *winRec = [[WSWindowRecord alloc] init];
                [winRec setOrigin:frame.origin];
                winRec.title = [NSString stringWithCString:req->title];
                winRec.level = kCGNormalWindowLevelKey;
                winRec.state = NORMAL;

                [_windows[kCGNormalWindowLevelKey] addObject:winRec];
                curWindow = winRec;

                rep.ret_code = 0;
                rep.window_id = (unsigned int)[_windows[kCGNormalWindowLevelKey] count];
                rep.event_port = req->flags;
                rep.surface_addr = 0;
                mach_msg((mach_msg_header_t *)&rep, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(rep), 0, MACH_PORT_NULL, 50, MACH_PORT_NULL);
                return YES;
            }
            case MSG_ID_RPC: { // new style synchronous RPC calls
                mach_port_t reply = MACH_PORT_NULL;
                if(msg.portMsg.msgh_descriptor_count > 0)
                    reply = msg.portMsg.descriptor.name;
                pid_t pid = msg.portMsg.pid;
                struct wsRPCBase *base = (struct wsRPCBase *)msg.portMsg.data;

                printf("[WindowServer] Received RPC code %u from pid %u (reply port=%u)\n",
                       base ? base->code : 0, pid, reply);
                fflush(stdout);

                if(base->len > sizeof(msg.portMsg.data) - sizeof(struct wsRPCBase)) {
                    printf("[WindowServer] Rejected RPC code %u request with oversized data block of %u bytes\n",
                           base->code, base->len);
                    return NO;
                }

                switch(base->code) {
                    // Quartz Display Services (CoreGraphics)
                    case kCGMainDisplayID: [self rpcMainDisplayID:&msg.portMsg]; break;
                    case kCGGetOnlineDisplayList: [self rpcGetOnlineDisplayList:&msg.portMsg]; break;
                    case kCGGetActiveDisplayList: [self rpcGetActiveDisplayList:&msg.portMsg]; break;
                    case kCGGetDisplaysWithOpenGLDisplayMask: [self rpcGetDisplaysWithOpenGLDisplayMask:&msg.portMsg];
                                                              break;
                    case kCGGetDisplaysWithPoint: [self rpcGetDisplaysWithPoint:&msg.portMsg]; break;
                    case kCGGetDisplaysWithRect: [self rpcGetDisplaysWithRect:&msg.portMsg]; break;
                    case kCGOpenGLDisplayMaskToDisplayID: [self rpcOpenGLDisplayMaskToDisplayID:&msg.portMsg];
                                                          break;
                    case kCGDisplayIDToOpenGLDisplayMask: [self rpcDisplayIDToOpenGLDisplayMask:&msg.portMsg];
                                                          break;
                    case kCGDisplayCaptureWithOptions: [self rpcDisplayCapture:&msg.portMsg]; break;
                    case kCGDisplayRelease: [self rpcDisplayRelease:&msg.portMsg]; break;
                    case kCGCaptureAllDisplaysWithOptions: [self rpcCaptureAllDisplays:&msg.portMsg]; break;
                    case kCGReleaseAllDisplays: [self rpcReleaseAllDisplays:&msg.portMsg]; break;
                    case kCGDisplayGetDrawingContext: [self rpcDisplayGetDrawingContext:&msg.portMsg]; break;
                    case kCGDisplayCreateImageForRect: [self rpcDisplayCreateImageForRect:&msg.portMsg]; break;
                    case kCGDisplayBounds: [self rpcDisplayBounds:&msg.portMsg]; break;
                    case kCGDisplayStateFlags: [self rpcDisplayStateFlags:&msg.portMsg]; break; 
                    case kCGDisplayMirrorsDisplay: [self rpcDisplayMirrorsDisplay:&msg.portMsg]; break; 
                    case kCGDisplayModelNumber: [self rpcDisplayModelNumber:&msg.portMsg]; break; 
                    case kCGDisplayPrimaryDisplay: [self rpcDisplayPrimaryDisplay:&msg.portMsg]; break; 
                    case kCGDisplayRotation: [self rpcDisplayRotation:&msg.portMsg]; break; 
                    case kCGDisplayScreenSize: [self rpcDisplayScreenSize:&msg.portMsg]; break; 
                    case kCGDisplaySerialNumber: [self rpcDisplaySerialNumber:&msg.portMsg]; break; 
                    case kCGDisplayUnitNumber: [self rpcDisplayUnitNumber:&msg.portMsg]; break; 
                    case kCGDisplayVendorNumber: [self rpcDisplayVendorNumber:&msg.portMsg]; break; 
                    case kCGCompleteDisplayConfiguration: [self rpcCompleteDisplayConfiguration:&msg.portMsg]; break;
                    case kCGRestorePermanentDisplayConfiguration: [self rpcRestorePermanentDisplayConfiguration:&msg.portMsg]; break;
                    case kCGDisplayCopyDisplayMode: [self rpcDisplayCopyDisplayMode:&msg.portMsg]; break;
                    case kCGDisplayCopyAllDisplayModes: [self rpcDisplayCopyAllDisplayModes:&msg.portMsg]; break;
                    case kCGDisplaySetDisplayMode: [self rpcDisplaySetDisplayMode:&msg.portMsg]; break;
                    case kCGSetDisplayTransferByFormula: [self rpcSetDisplayTransferByFormula:&msg.portMsg]; break;
                    case kCGGetDisplayTransferByFormula: [self rpcGetDisplayTransferByFormula:&msg.portMsg]; break;
                    case kCGSetDisplayTransferByTable: [self rpcSetDisplayTransferByTable:&msg.portMsg]; break;
                    case kCGGetDisplayTransferByTable: [self rpcGetDisplayTransferByTable:&msg.portMsg]; break;
                    case kCGSetDisplayTransferByByteTable: [self rpcSetDisplayTransferByByteTable:&msg.portMsg]; break;
                    case kCGDisplayRestoreColorSyncSettings: [self rpcDisplayRestoreColorSyncSettings:&msg.portMsg]; break;
                    case kCGDisplayGammaTableCapacity: [self rpcDisplayGammaTableCapacity:&msg.portMsg]; break;
                    case kCGDisplayHideCursor: [self rpcDisplayHideCursor:&msg.portMsg]; break;
                    case kCGDisplayShowCursor: [self rpcDisplayShowCursor:&msg.portMsg]; break;
                    case kCGDisplayMoveCursorToPoint: [self rpcDisplayMoveCursorToPoint:&msg.portMsg]; break;
                    case kCGAssociateMouseAndMouseCursorPosition: [self rpcAssociateMouseAndMouseCursorPosition:&msg.portMsg]; break;
                    case kCGWarpMouseCursorPosition: [self rpcWarpMouseCursorPosition:&msg.portMsg]; break;
                    case kCGGetLastMouseDelta: [self rpcGetLastMouseDelta:&msg.portMsg]; break;
                    // Window Management (AppKit)
                    case kWSWindowCreate: [self rpcWindowCreate:&msg.portMsg]; break;
                    case kWSWindowDestroy: [self rpcWindowDestroy:&msg.portMsg]; break;
                    case kWSWindowModifyState: [self rpcWindowModifyState:&msg.portMsg]; break;
                    // App management (Dock)
                    case kWSApplicationActivate: [self rpcApplicationActivate:&msg.portMsg]; break;
                }
                break;
            }
            case MSG_ID_PORT:
            {
                mach_port_t port = msg.portMsg.descriptor.name;
                pid_t pid = msg.portMsg.pid;
                const char *rawBundle = msg.portMsg.bundleID;
                NSString *bundleID = (rawBundle && rawBundle[0]) ? [NSString stringWithCString:rawBundle] : nil;
                if (!bundleID) {
                    if (pid == 3) bundleID = @"com.ravynos.SystemUIServer";
                    else if (pid == 4 || pid == 5) bundleID = @"com.ravynos.Dock";
                    else bundleID = [NSString stringWithFormat:@"com.unknown.app%d", pid];
                }
                if(logLevel >= WS_INFO)
                    NSLog(@"Port registration received from %@ pid %u for port %u", bundleID, pid, port);
                WSAppRecord *rec = [apps objectForKey:bundleID];
                if(!rec) {
                    rec = [WSAppRecord new];
                    rec.bundleID = bundleID;
                    rec.port = port;
                    if([bundleID isEqualToString:@"com.ravynos.SystemUIServer"] ||
                            [bundleID isEqualToString:@"com.ravynos.Dock"])
                        [rec skipSwitcher:YES];
                }
                rec.pid = pid;
                rec.path = _pathForPID(pid);
                if(!rec.path) rec.path = @"";
                if(port != rec.port && logLevel >= WS_WARNING)
                    NSLog(@"Port registration received for %@ pid %u when already registered (%u -> %u)",
                            rec.bundleID, pid, rec.port, port);
                [apps setObject:rec forKey:bundleID];

                Message repMsg = {0};
                WSAppRecord *dock = [apps objectForKey:@"com.ravynos.Dock"];
                if(dock != nil && [dock port] != MACH_PORT_NULL) {
                    repMsg.header.msgh_remote_port = [dock port];
                    repMsg.header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND, 0, 0, 0);
                    repMsg.header.msgh_id = MSG_ID_INLINE;
                    repMsg.header.msgh_size = sizeof(repMsg) - sizeof(mach_msg_trailer_t);
                    repMsg.code = CODE_APP_LAUNCHED;
                    repMsg.pid = pid;
                    repMsg.len = [rec.path length];
                    const char *pstr = [rec.path UTF8String];
                    if (!pstr) pstr = [rec.path cString];
                    if (pstr) strncpy(repMsg.data, pstr, PATH_MAX);
                    const char *bstr = [bundleID UTF8String];
                    if (!bstr) bstr = [bundleID cString];
                    if (bstr) strncpy(repMsg.bundleID, bstr, sizeof(repMsg.bundleID));
                    mach_msg((mach_msg_header_t *)&repMsg, MACH_SEND_MSG|MACH_SEND_TIMEOUT,
                            sizeof(repMsg) - sizeof(mach_msg_trailer_t),
                            0, MACH_PORT_NULL, 100 /* ms timeout */, MACH_PORT_NULL);
                }

                [self watchForProcessExit:pid];

                // A newly-launched app becomes the active one
                if(![rec skipSwitcher]) {
                    [self deactivateApp:curApp];
                    curApp = [apps objectForKey:bundleID];
                    [self activateApp:curApp];
                }
                break;
            }
            case MSG_ID_INLINE:
            {
                switch(msg.msg.code) {
                    case CODE_MENU_FOR_APP:
                    {
                        Message menuMsg = {0};
                        WSAppRecord *app = [apps objectForKey:@"com.ravynos.SystemUIServer"];
                        if(app == nil) {
                            NSLog(@"Cannot install menus for client - is SystemUIServer running?");
                            break;
                        }
                        menuMsg.header.msgh_remote_port = [app port];
                        menuMsg.header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND, 0, 0, 0);
                        menuMsg.header.msgh_id = MSG_ID_INLINE;
                        menuMsg.header.msgh_size = sizeof(menuMsg) - sizeof(mach_msg_trailer_t);
                        menuMsg.code = msg.msg.code;
                        menuMsg.pid = msg.msg.pid;
                        strncpy(menuMsg.bundleID, msg.msg.bundleID, sizeof(menuMsg.bundleID));
                        memcpy(menuMsg.data, msg.msg.data, msg.msg.len);
                        menuMsg.len = msg.msg.len;
                        mach_msg((mach_msg_header_t *)&menuMsg, MACH_SEND_MSG|MACH_SEND_TIMEOUT,
                                sizeof(menuMsg) - sizeof(mach_msg_trailer_t),
                                0, MACH_PORT_NULL, 100 /* ms timeout */, MACH_PORT_NULL);
                        break;
                    }
                    case CODE_ITEM_CLICKED:
                    {
                        if(curApp == nil) {
                            NSLog(@"Dropping menu click because curApp is nil!");
                            break;
                        }
                        [self sendInlineData:msg.msg.data
                                      length:msg.msg.len
                                    withCode:msg.msg.code
                                      toPort:[curApp port]];

                    }
                    case CODE_ADD_RECENT_ITEM:
                        // FIXME: pass to SystemUIServer
                        break;
                    case CODE_APP_HIDE:
                    {
                        pid_t pid;
                        memcpy(&pid, msg.msg.data, sizeof(int));
                        NSLog(@"CODE_APP_HIDE: pid = %d", pid);
                        // FIXME: pass to SystemUIServer
                        mach_port_t port = 0; // FIXME: get from active app
                        if(port != MACH_PORT_NULL) {
                            Message activate = {0};
                            activate.header.msgh_remote_port = port;
                            activate.header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND, 0, 0, 0);
                            activate.header.msgh_id = MSG_ID_INLINE;
                            activate.header.msgh_size = sizeof(activate) - sizeof(mach_msg_trailer_t);
                            activate.code = msg.msg.code;
                            activate.len = 0;
                            mach_msg((mach_msg_header_t *)&activate, MACH_SEND_MSG|MACH_SEND_TIMEOUT,
                                sizeof(activate) - sizeof(mach_msg_trailer_t),
                                0, MACH_PORT_NULL, 100 /* ms timeout */, MACH_PORT_NULL);
                        }
                        break;
                    }
		    case CODE_ADD_STATUS_ITEM:
		    {
			NSData *data = [NSData
			    dataWithBytes:msg.msg.data length:msg.msg.len];
			NSObject *o = nil;
			@try {
			    o = [NSKeyedUnarchiver unarchiveObjectWithData:data];
			}
			@catch(NSException *localException) {
			    NSLog(@"%@",localException);
			}

			if(o == nil || [o isKindOfClass:[NSDictionary class]] == NO ||
			    [(NSDictionary *)o objectForKey:@"StatusItem"] == nil ||
			    [(NSDictionary *)o objectForKey:@"ProcessID"] == nil) {
			    fprintf(stderr, "archiver: bad input\n");
			    break;
			}

                        // FIXME: send to SystemUIServer
			break;
		    }
                    default:
                        NSLog(@"Unhandled WindowServer code %u", msg.msg.code);
                }
                break;
            }
        }
        return YES;
    }
}

// called from our kq watcher thread
- (void)processKernelQueue {
    struct kevent out[128];
    int count = kevent(_kq, NULL, 0, out, 128, NULL);

    for(int i = 0; i < count; ++i) {
        switch(out[i].filter) {
            case EVFILT_PROC:
                if((out[i].fflags & NOTE_EXIT)) {
                    //NSLog(@"PID %lu exited", out[i].ident);
                    WSAppRecord *app = [self findAppByPID:out[i].ident];
                    if(app == nil) {
                        NSLog(@"PID %u exited, but no matching app record", out[i].ident);
                        break;
                    }

                    if(curApp == app) {
                        [self switchApp];
                        if(curApp == app) {
                            curApp = nil; // there was nothing to switch to
                            curWindow = nil;
                        }
                    }

                    [apps removeObjectForKey:app.bundleID];
                    for(int x = 0; x < [[app windows] count]; ++x)
                        [self removeWindowFromAllLevels:[[app windows] objectAtIndex:x]];
                    [app removeAllWindows];

                    WSAppRecord *sysui = [apps objectForKey:@"com.ravynos.SystemUIServer"];
                    if(sysui != nil)
                        notifyAppExited([sysui port], out[i].ident,
                                [app.bundleID UTF8String], [app.path UTF8String]);

                    WSAppRecord *dock = [apps objectForKey:@"com.ravynos.Dock"];
                    if(dock != nil)
                        notifyAppExited([dock port], out[i].ident,
                                [app.bundleID UTF8String], [app.path UTF8String]);
                }
                break;
            default:
                NSLog(@"unknown filter");
        }
    }
}

- (WSWindowRecord *)windowUnderPointer:(NSPoint)pos app:(WSAppRecord **)app {
    // We need to work from foreground (highest level) to background (0) to
    // find the foremost window. Otherwise we might be choosing something
    // that is behind another.

    for(int level = kCGNumReservedWindowLevels - 1; level >= 0; --level) {
        NSArray *wins = _windows[level];
        int count = [wins count];
        for(int i = 0; i < count; ++i) {
            WSWindowRecord *win = [wins objectAtIndex:i];
            if(win.state == HIDDEN || win.state == MINIMIZED || win.state == CLOSED)
                continue;
            if(NSPointInRect(pos, win.frame)) {
                *app = win.app;
                return win;
            }
        }
    }

    return nil;
}

static void ws_handle_mouse_click(WindowServer *self, NSPoint pos) {
    int x = (int)pos.x, y = (int)pos.y;

    // 1. MenuBar & Apple Menu
    if (y >= 0 && y <= 24) {
        if (x >= 0 && x <= 30) {
            s_apple_menu_open = !s_apple_menu_open;
            return;
        }
        s_apple_menu_open = NO;
        return;
    }

    // 2. Apple Menu dropdown clicks
    if (s_apple_menu_open) {
        if (x >= 6 && x <= 186 && y >= 26 && y <= 186) {
            if (y < 48) { // About
                s_about_open = YES;
                s_active_window = 3;
            } else if (y >= 75 && y < 92) { // Terminal
                s_term_open = YES;
                s_active_window = 1;
            } else if (y >= 92 && y < 110) { // Calculator
                s_calc_open = YES;
                s_active_window = 2;
            }
            s_apple_menu_open = NO;
            return;
        }
        s_apple_menu_open = NO;
    }

    // 3. Dock icon clicks
    int dock_w = 380, dock_h = 58;
    int scr_w = (int)self->_geometry.size.width;
    int scr_h = (int)self->_geometry.size.height;
    int dock_x = (scr_w - dock_w) / 2;
    int dock_y = scr_h - 70;

    if (y >= dock_y && y <= dock_y + dock_h) {
        int ix0 = dock_x + 14;
        int ix1 = dock_x + 66;
        int ix2 = dock_x + 118;
        int ix3 = dock_x + 170;
        int ix4 = dock_x + 222;

        if (x >= ix0 && x < ix0 + 42) {
            s_active_window = 0;
        } else if (x >= ix2 && x < ix2 + 42) {
            s_term_open = !s_term_open;
            if (s_term_open) s_active_window = 1;
        } else if (x >= ix3 && x < ix3 + 42) {
            s_calc_open = !s_calc_open;
            if (s_calc_open) s_active_window = 2;
        } else if (x >= ix4 && x < ix4 + 42) {
            s_about_open = !s_about_open;
            if (s_about_open) s_active_window = 3;
        }
        return;
    }

    // 4. Calculator interactive button clicks
    if (s_calc_open && x >= s_calc_frame.origin.x && x <= s_calc_frame.origin.x + s_calc_frame.size.width &&
        y >= s_calc_frame.origin.y + 28 && y <= s_calc_frame.origin.y + s_calc_frame.size.height) {
        s_active_window = 2;
        int cx = (int)s_calc_frame.origin.x;
        int cy = (int)s_calc_frame.origin.y;

        const char *btn_labels[5][4] = {
            {"C", "+/-", "%", "/"},
            {"7", "8", "9", "*"},
            {"4", "5", "6", "-"},
            {"1", "2", "3", "+"},
            {"0", "", ".", "="}
        };

        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < 4; c++) {
                if (r == 4 && c == 1) continue;
                int bx = cx + 12 + c * 54;
                int by = cy + 88 + r * 46;
                int bw = (r == 4 && c == 0) ? 102 : 48;
                int bh = 40;

                if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) {
                    const char *lbl = btn_labels[r][c];
                    if (strcmp(lbl, "C") == 0) {
                        s_calc_val = 0; s_calc_acc = 0; s_calc_op = 0;
                        strcpy(s_calc_str, "0");
                    } else if (lbl[0] >= '0' && lbl[0] <= '9') {
                        int d = lbl[0] - '0';
                        if (strcmp(s_calc_str, "0") == 0 || strcmp(s_calc_str, "+") == 0 ||
                            strcmp(s_calc_str, "-") == 0 || strcmp(s_calc_str, "*") == 0 ||
                            strcmp(s_calc_str, "/") == 0) {
                            s_calc_val = d;
                            sprintf(s_calc_str, "%d", d);
                        } else {
                            s_calc_val = s_calc_val * 10 + d;
                            sprintf(s_calc_str, "%d", s_calc_val);
                        }
                    } else if (strcmp(lbl, "+") == 0) {
                        s_calc_acc = s_calc_val; s_calc_val = 0; s_calc_op = 1;
                        strcpy(s_calc_str, "+");
                    } else if (strcmp(lbl, "-") == 0) {
                        s_calc_acc = s_calc_val; s_calc_val = 0; s_calc_op = 2;
                        strcpy(s_calc_str, "-");
                    } else if (strcmp(lbl, "*") == 0) {
                        s_calc_acc = s_calc_val; s_calc_val = 0; s_calc_op = 3;
                        strcpy(s_calc_str, "*");
                    } else if (strcmp(lbl, "/") == 0) {
                        s_calc_acc = s_calc_val; s_calc_val = 0; s_calc_op = 4;
                        strcpy(s_calc_str, "/");
                    } else if (strcmp(lbl, "=") == 0) {
                        int res = s_calc_val;
                        if (s_calc_op == 1) res = s_calc_acc + s_calc_val;
                        else if (s_calc_op == 2) res = s_calc_acc - s_calc_val;
                        else if (s_calc_op == 3) res = s_calc_acc * s_calc_val;
                        else if (s_calc_op == 4 && s_calc_val != 0) res = s_calc_acc / s_calc_val;
                        s_calc_val = res;
                        s_calc_acc = 0;
                        s_calc_op = 0;
                        sprintf(s_calc_str, "%d", res);
                    }
                    return;
                }
            }
        }
        return;
    }

    // 5. Traffic light buttons & Titlebar dragging
    // Terminal window titlebar
    if (s_term_open && x >= s_term_frame.origin.x && x <= s_term_frame.origin.x + s_term_frame.size.width &&
        y >= s_term_frame.origin.y && y <= s_term_frame.origin.y + 28) {
        s_active_window = 1;
        if (x >= s_term_frame.origin.x + 6 && x <= s_term_frame.origin.x + 20) { // Red close
            s_term_open = NO;
            return;
        }
        s_in_drag = YES;
        s_drag_win = 1;
        s_drag_off_x = x - (int)s_term_frame.origin.x;
        s_drag_off_y = y - (int)s_term_frame.origin.y;
        return;
    }

    // Calculator window titlebar
    if (s_calc_open && x >= s_calc_frame.origin.x && x <= s_calc_frame.origin.x + s_calc_frame.size.width &&
        y >= s_calc_frame.origin.y && y <= s_calc_frame.origin.y + 28) {
        s_active_window = 2;
        if (x >= s_calc_frame.origin.x + 6 && x <= s_calc_frame.origin.x + 20) { // Red close
            s_calc_open = NO;
            return;
        }
        s_in_drag = YES;
        s_drag_win = 2;
        s_drag_off_x = x - (int)s_calc_frame.origin.x;
        s_drag_off_y = y - (int)s_calc_frame.origin.y;
        return;
    }

    // About window titlebar
    if (s_about_open && x >= s_about_frame.origin.x && x <= s_about_frame.origin.x + s_about_frame.size.width &&
        y >= s_about_frame.origin.y && y <= s_about_frame.origin.y + 28) {
        s_active_window = 3;
        if (x >= s_about_frame.origin.x + 6 && x <= s_about_frame.origin.x + 20) { // Red close
            s_about_open = NO;
            return;
        }
        s_in_drag = YES;
        s_drag_win = 3;
        s_drag_off_x = x - (int)s_about_frame.origin.x;
        s_drag_off_y = y - (int)s_about_frame.origin.y;
        return;
    }

    // Focus on window body click
    if (s_term_open && NSPointInRect(pos, s_term_frame)) s_active_window = 1;
    else if (s_calc_open && NSPointInRect(pos, s_calc_frame)) s_active_window = 2;
    else if (s_about_open && NSPointInRect(pos, s_about_frame)) s_active_window = 3;
}

- (BOOL)sendEventToApp:(struct mach_event *)event {
    static BOOL inDrag = NO;
    static WSWindowRecord *dragWindow = nil;

    NSPoint pos = NSMakePoint(event->x, event->y);

    if(event->code == NSLeftMouseDown) {
        ws_handle_mouse_click(self, pos);
    } else if(event->code == NSLeftMouseDragged) {
        if(s_in_drag) {
            if(s_drag_win == 1) {
                s_term_frame.origin.x = pos.x - s_drag_off_x;
                s_term_frame.origin.y = pos.y - s_drag_off_y;
            } else if(s_drag_win == 2) {
                s_calc_frame.origin.x = pos.x - s_drag_off_x;
                s_calc_frame.origin.y = pos.y - s_drag_off_y;
            } else if(s_drag_win == 3) {
                s_about_frame.origin.x = pos.x - s_drag_off_x;
                s_about_frame.origin.y = pos.y - s_drag_off_y;
            }
            return YES;
        }
    } else if(event->code == NSLeftMouseUp) {
        s_in_drag = NO;
        s_drag_win = 0;
    }

    /* Any key input goes to active window & app, even if pointer is over something else
     * Otherwise, identify the window (if any) that is under the pointer. Windows on macOS
     * seem to receive mouse and scroll inputs when not the active window.
     */
    WSAppRecord *app = curApp;
    WSWindowRecord *window = nil;
    if(event->code == NSKeyDown || event->code == NSKeyUp)
        event->windowID = curWindow.number;
    else
        window = [self windowUnderPointer:pos app:&app];

    NSRect titleFrame = NSZeroRect;
    if(window != nil) {
        titleFrame = window.geometry;
        titleFrame.origin.y += titleFrame.size.height - WSWindowTitleHeight;
        titleFrame.size.height = WSWindowTitleHeight;
    }

    // First, check if we want to handle this event ourselves!
    switch(event->code) {
        case NSLeftMouseDragged: {
            // We are already dragging a window, so keep at it
            if(inDrag && dragWindow != nil) {
                [dragWindow moveByX:event->dx Y:event->dy];
                [self updateClientWindowState:dragWindow];
                return YES;
            }

            // Are we dragging a window at all?
            if(window == nil)
                return YES;

            // Are we dragging the titlebar?
            if(NSPointInRect(pos, titleFrame)) {
                [window moveByX:event->dx Y:event->dy];
                [self updateClientWindowState:window];
                inDrag = YES;
                dragWindow = window;
                return YES;
            }

            // Handled all WS cases - send this to the window!
            break;
        }
        case NSLeftMouseDown: {
            if(window == nil)
                return YES;
            
            // Did we click in the titlebar but not a button?
            NSRect noControl = titleFrame;
            noControl.origin.x += NSWindowControlSpacing*3;
            noControl.origin.x += NSWindowControlDiameter*3;
            if(window.state == NORMAL && NSPointInRect(pos, noControl)) {
                [window moveByX:event->dx Y:event->dy];
                //[self updateClientWindowState:window];
                inDrag = YES;
                dragWindow = window;
                return YES;
            }

            if(![app skipSwitcher]) {
                [self deactivateApp:curApp];
                curApp = app;
                curWindow = window;
                [self activateApp:curApp];
            }
            break;
        }
        case NSLeftMouseUp: {
            if(inDrag) {
                [self updateClientWindowState:dragWindow];
                inDrag = NO;
                dragWindow = nil;
                return YES;
            }
        }
    }

    if(app == nil)
        return YES;

    event->windowID = window.number;

    return [self sendInlineData:event
                         length:sizeof(struct mach_event)
                       withCode:CODE_INPUT_EVENT
                          toPort:[app port]];
}

- (void)updateClientWindowState:(WSWindowRecord *)window {
    struct wsRPCWindow data = {0};
    data.base.code = kWSWindowModifyState;
    data.base.len = sizeof(data) - sizeof(struct wsRPCBase);
    data.windowID = window.number;
    data.x = window.geometry.origin.x;
    data.y = window.geometry.origin.y;
    data.w = window.geometry.size.width;
    data.h = window.geometry.size.height;
    data.style = window.styleMask;
    data.state = window.state;
    // title is ignored - only client can set it

    const char *appKey = [window.shmPath cString];
    char *key = appKey + 1;
    while(*key != '/' && *key != '\0')
        key++;
    int len = key - (appKey + 1);
    key = malloc(len+1);
    memcpy(key, appKey+1, len);
    key[len] = 0;

    WSAppRecord *app = [apps objectForKey:[NSString stringWithCString:key]];
    if(app)
        [self sendInlineData:&data length:sizeof(data) withCode:CODE_WINDOW_STATE toPort:[app port]];
    else
        NSLog(@"Cannot send window state update to app: not found. %@", window);

    // Tell Dock if the window gets minimized or closed so it can manage icons
    if(window.state == MINIMIZED || window.state == CLOSED)
        [self notifyDock:&data length:sizeof(data) withCode:CODE_WINDOW_STATE forApp:app];
}

- (BOOL)notifyDock:(void *)data length:(int)length withCode:(int)code forApp:(WSAppRecord *)app {
    WSAppRecord *dock = [apps objectForKey:@"com.ravynos.Dock"];
    if(dock == nil) {
        if(logLevel >= WS_WARNING)
            NSLog(@"Cannot notify Dock - is it running?");
        return NO;
    }

    Message msg = {0};
    msg.header.msgh_remote_port = [dock port];
    msg.header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND, 0, 0, 0);
    msg.header.msgh_id = MSG_ID_INLINE;
    msg.header.msgh_size = sizeof(msg) - sizeof(mach_msg_trailer_t);
    msg.code = code;
    msg.pid = [app pid];
    strncpy(msg.bundleID, [app.bundleID UTF8String], sizeof(msg.bundleID)-1);

    memcpy(msg.data, data, length);
    msg.len = length;

    int ret;
    if((ret = mach_msg((mach_msg_header_t *)&msg, MACH_SEND_MSG|MACH_SEND_TIMEOUT,
        sizeof(msg) - sizeof(mach_msg_trailer_t), 0, MACH_PORT_NULL, 50 /* ms timeout */,
        MACH_PORT_NULL)) != MACH_MSG_SUCCESS) {
        if(logLevel >= WS_WARNING)
            NSLog(@"Failed to send message to Dock: 0x%x", ret);
        return NO;
    }
    return YES;
}

- (BOOL)sendInlineData:(void *)data length:(int)length withCode:(int)code toPort:(mach_port_t)port {
    Message *msg = (Message *)malloc(sizeof(Message));
    if(!msg) return NO;
    memset(msg, 0, sizeof(Message));

    msg->header.msgh_remote_port = port;
    msg->header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND, 0, 0, 0);
    msg->header.msgh_id = MSG_ID_INLINE;
    msg->header.msgh_size = sizeof(Message) - sizeof(mach_msg_trailer_t);
    msg->code = code;
    msg->pid = getpid();
    strncpy(msg->bundleID, WINDOWSERVER_SVC_NAME, sizeof(msg->bundleID)-1);

    if(data && length > 0)
        memcpy(msg->data, data, length);
    msg->len = length;

    printf("[WindowServer] sendInlineData: sending reply (code %d, len %d) to port 0x%x...\n", code, length, port);
    fflush(stdout);

    int ret = mach_msg((mach_msg_header_t *)msg, MACH_SEND_MSG|MACH_SEND_TIMEOUT,
        sizeof(Message) - sizeof(mach_msg_trailer_t), 0, MACH_PORT_NULL, 50 /* ms timeout */,
        MACH_PORT_NULL);
    free(msg);

    if(ret != MACH_MSG_SUCCESS) {
        printf("[WindowServer] sendInlineData: mach_msg to port 0x%x FAILED (ret=0x%x)\n", port, ret);
        fflush(stdout);
        return NO;
    }
    printf("[WindowServer] sendInlineData: mach_msg to port 0x%x SUCCEEDED\n", port);
    fflush(stdout);
    return YES;
}

- (void)watchForProcessExit:(unsigned int)pid {
    struct kevent kev[1];
    EV_SET(kev, pid, EVFILT_PROC, EV_ADD|EV_ONESHOT, NOTE_EXIT, 0, NULL);
    kevent(_kq, kev, 1, NULL, 0, NULL);
}

- (WSAppRecord *)findAppByPID:(unsigned int)pid {
    NSEnumerator *apprecs = [apps objectEnumerator];
    WSAppRecord *app;
    while((app = [apprecs nextObject]) != nil) {
        if(app.pid == pid)
            return app;
    }
    return nil;
}

-(void)deactivateApp:(WSAppRecord *)app {
    if(app == nil)
        return;

    Message msg = {0};
    struct mach_activation_data data = {0};
    WSAppRecord *sysui = [apps objectForKey:@"com.ravynos.SystemUIServer"];
    if(sysui == nil) {
        NSLog(@"cannot notify for deactivated app - is SystemUIServer running?");
        return;
    }
    msg.header.msgh_remote_port = [sysui port];
    msg.header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND, 0, 0, 0);
    msg.header.msgh_id = MSG_ID_INLINE;
    msg.header.msgh_size = sizeof(msg) - sizeof(mach_msg_trailer_t);
    msg.code = CODE_ACTIVATION_STATE;
    msg.pid = getpid();
    strncpy(msg.bundleID, [[app bundleID] UTF8String], sizeof(msg.bundleID));
    memcpy(msg.data, &data, sizeof(data)); // window ID
    msg.len = sizeof(data);
    mach_msg((mach_msg_header_t *)&msg, MACH_SEND_MSG|MACH_SEND_TIMEOUT,
            sizeof(msg) - sizeof(mach_msg_trailer_t),
            0, MACH_PORT_NULL, 100 /* ms timeout */, MACH_PORT_NULL);
}

-(void)activateApp:(WSAppRecord *)app {
    if(app == nil)
        return;

    Message msg = {0};
    struct mach_activation_data data = {0, 1};
    WSAppRecord *sysui = [apps objectForKey:@"com.ravynos.SystemUIServer"];
    if(sysui == nil) {
        NSLog(@"cannot notify for activated app - is SystemUIServer running?");
        return;
    }
    msg.header.msgh_remote_port = [sysui port];
    msg.header.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND, 0, 0, 0);
    msg.header.msgh_id = MSG_ID_INLINE;
    msg.header.msgh_size = sizeof(msg) - sizeof(mach_msg_trailer_t);
    msg.code = CODE_ACTIVATION_STATE;
    msg.pid = getpid();
    strncpy(msg.bundleID, [[app bundleID] UTF8String], sizeof(msg.bundleID));
    memcpy(msg.data, &data, sizeof(data));
    msg.len = sizeof(data);
    mach_msg((mach_msg_header_t *)&msg, MACH_SEND_MSG|MACH_SEND_TIMEOUT,
            sizeof(msg) - sizeof(mach_msg_trailer_t),
            0, MACH_PORT_NULL, 100 /* ms timeout */, MACH_PORT_NULL);
}

// FIXME: do some visual magic here for the user
- (void)switchApp {
    WSAppRecord *oldApp = curApp;

    WSAppRecord *app;
    NSMutableArray *viableApps = [NSMutableArray new];
    NSArray *list = [apps allValues];
    for(int i = 0; i < [list count]; ++i) {
        WSAppRecord *app = [list objectAtIndex:i];
        if([app skipSwitcher])
            continue;
        [viableApps addObject:app];
    }

    for(int i = 0; i < [viableApps count]; ++i) {
        app = [viableApps objectAtIndex:i];
        if(app == curApp) {
            if(i+1 >= [viableApps count])
                curApp = [viableApps objectAtIndex:0];
            else
                curApp = [viableApps objectAtIndex:1+i];
            break;
        }
    }
    
    [self switchFromApp:oldApp];
}

-(void)switchFromApp:(WSAppRecord *)oldApp toWindow:(WSWindowRecord *)win {
    struct mach_activation_data data = {0};
    if(oldApp != curApp) {
        // Inform the old app that it has become inactive
        [self sendInlineData:&data
                      length:sizeof(data)
                    withCode:CODE_ACTIVATION_STATE
                      toPort:[oldApp port]];

        // Now tell SystemUIServer the app resigned active
        [self deactivateApp:oldApp];
    }

    curWindow = nil;
    if(curApp == nil)
        return;

    if(win != nil)
        curWindow = win;
    else {
        // Find the first non-hidden window for the newly active app
        for(int i = 0; i < [[curApp windows] count]; ++i) {
            WSWindowRecord *win = [[curApp windows] objectAtIndex:i];
            if(win.state == HIDDEN || win.state == MINIMIZED || win.state == CLOSED)
                continue;
            curWindow = win;
            break;
        }
    }

    if(curApp == oldApp)
        return;

    // Inform the now-active app of its status
    data.windowID = (curWindow == nil) ? 0 : curWindow.number;
    data.active = 1;
    [self sendInlineData:&data
                  length:sizeof(data)
                withCode:CODE_ACTIVATION_STATE
                   toPort:[curApp port]];

    // Now tell SystemUIServer the app became active
    [self activateApp:curApp];
}

-(void)switchFromApp:(WSAppRecord *)oldApp {
    [self switchFromApp:oldApp toWindow:nil];
}

-(void)signalQuit {
    [self performLogout:0];
    pid_t pid = fork();
    if(pid == 0)
        execl("/bin/launchctl", "launchctl", "remove", "com.ravynos.WindowServer", NULL);
    else
        waitpid(pid, NULL, 0);
    ready = NO;
}

@end

