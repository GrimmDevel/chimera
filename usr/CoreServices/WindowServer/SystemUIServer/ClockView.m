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

#include <pthread.h>
#include <unistd.h>
#include <locale.h>
#import <AppKit/AppKit.h>
#import "desktop.h"

const NSString *PrefsDateFormatStringKey = @"DateFormatString";
const NSString *defaultFormatEN = @"%a %b %d  %I:%M %p";
pthread_mutex_t mtx;

@interface NSApplication(SystemUIServer)
-(int)_wakeUp;
@end

@implementation ClockView
- initWithFrame:(NSRect)frame {
    NSUserDefaults *prefs = [NSUserDefaults standardUserDefaults];
    dateFormat = [prefs stringForKey:PrefsDateFormatStringKey];
    if(dateFormat == nil || [dateFormat length] == 0) {
        dateFormat = defaultFormatEN;
    }
    NSLocale *locale = [NSLocale currentLocale];
    dateFormatter = [[NSDateFormatter alloc] initWithDateFormat:dateFormat 
        allowNaturalLanguage:YES locale:locale];

    NSFont *font = [NSFont systemFontOfSize:15];
    if(font == nil)
        font = [NSFont userFontOfSize:15];
    NSColor *color = [NSColor blackColor];
    if(color == nil)
        color = [NSColor textColor];

    if(font != nil && color != nil) {
        attributes = [NSDictionary
            dictionaryWithObjects:@[font, color]
                          forKeys:@[NSFontAttributeName, NSForegroundColorAttributeName]];
    } else {
        attributes = [NSDictionary new];
    }

    NSString *val = [self currentDateValue];
    if(val == nil) val = @"Sat 02:30";
    dateString = [[NSAttributedString alloc] initWithString:val attributes:attributes];

    NSSize sz = [dateString size];
    sz.width += menuBarHPad;
    self = [super initWithFrame:NSMakeRect(frame.size.width - sz.width, menuBarVPad,
            sz.width + menuBarHPad, sz.height)];
    sz.width += menuBarHPad;

    pthread_mutex_init(&mtx, NULL);

    [NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(timerTick:) userInfo:nil repeats:YES];
    return self;
}

- (NSString *)currentDateValue {
    NSString *s = nil;
    if(dateFormatter != nil)
        s = [dateFormatter stringForObjectValue:[NSDate date]];
    if(s == nil || [s length] == 0)
        s = @"Sat 02:30";
    return s;
}

- (void)timerTick:(NSTimer *)timer {
    [self setNeedsDisplay:YES];
}

- (void)notifyTick:(id)arg {
    [self setNeedsDisplay:YES];
}

-(void)drawRect:(NSRect)rect {
    printf("[ClockView PID %d] drawRect: rect=(%f,%f,%fx%f)\n", getpid(), rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
    fflush(stdout);
    [super drawRect:rect];
    dateString = [[NSAttributedString alloc] initWithString:[self currentDateValue] attributes:attributes];
    [dateString drawInRect:rect];
}

- (NSSize)size {
    return _frame.size;
}

- (BOOL)refusesFirstResponder {
	return YES;
}

@end

