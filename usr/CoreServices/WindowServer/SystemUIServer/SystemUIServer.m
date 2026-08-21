/*
 * Copyright (C) 2022-2024 Zoe Knox <zoe@pixin.net>
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

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#include <stdlib.h>
#include <unistd.h>
#include <desktop.h>

const NSString *NSMenuDidUpdateNotification = @"NSMenuDidUpdate";
const NSString *NSApplicationDidQuitNotification = @"NSApplicationDidQuit";

int exitCode = 0;

int main(int argc, const char *argv[]) {
    printf("[SystemUIServer] Starting up (argc=%d)...\n", argc);
    fflush(stdout);
    __NSInitializeProcess(argc, argv);

    NSAutoreleasePool *pool = [NSAutoreleasePool new];
    [NSApplication sharedApplication];
    printf("[SystemUIServer] NSApplication initialized\n");
    fflush(stdout);

    AppDelegate *del = [AppDelegate new];
    if(!del) {
        printf("[SystemUIServer] Failed to allocate AppDelegate\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    printf("[SystemUIServer] AppDelegate initialized\n");
    fflush(stdout);

    // kick off a per-user launchd to invoke LaunchAgents and per-user LaunchDaemons
    // this starts Filer and Dock to establish the desktop session
    NSString *kickerPath = [[NSBundle mainBundle] pathForResource:@"kickSession" ofType:@""];
    if(kickerPath) {
        NSLog(@"kicking off session");
        system([kickerPath UTF8String]);
    }

    [pool drain];
    [NSApp setDelegate:del];
    printf("[SystemUIServer] Entering [NSApp run]...\n");
    fflush(stdout);
    [NSApp run];
    exit(exitCode);
}

