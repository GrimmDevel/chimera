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

#import <unistd.h>
#import "common.h"
#import "WindowServer.h"
#import <sys/event.h>
#import <termios.h>
#import <servers/bootstrap.h>
#import "message.h"

#define FINISH(x) ret=(x); goto __finish;

extern int optopt;
static jmp_buf jb;

void *machSvcLoop(void *arg) {
    WindowServer *ws = (__bridge WindowServer *)arg;
    while(1)
        [ws receiveMachMessage];
}

void *kqSvcLoop(void *arg) {
    WindowServer *ws = (__bridge WindowServer *)arg;
    while(1)
        [ws processKernelQueue];
} 

static void crashHandler(int sig) {
    longjmp(jb, SIGSEGV);
}

int main(int argc, const char *argv[]) {
    printf("\n[WindowServer] ========================================\n");
    printf("[WindowServer] Starting Apple Cocoa WindowServer...\n");
    printf("[WindowServer] (Mach IPC Compositor & Onyx2D Graphics)\n");
    printf("[WindowServer] ========================================\n");
    fflush(stdout);

    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    printf("[WindowServer] Instantiating WindowServer...\n");
    fflush(stdout);

    WindowServer *ws = [WindowServer new];
    if(ws == nil) {
        printf("[WindowServer] FATAL: Failed to initialize WindowServer!\n");
        fflush(stdout);
        exit(1);
    }

    printf("[WindowServer] Starting main compositor event loop...\n");
    fflush(stdout);

    [ws setShell:NONE];
    [ws run];
    ws = nil;
    return 0;
}

