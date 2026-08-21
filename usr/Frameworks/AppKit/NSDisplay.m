/* Copyright (c) 2006-2007 Christopher J. W. Lloyd <cjwl@objc.net>
   Copyright (C) 2022-2024 Zoe Knox <zoe@ravynsoft.com>

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE. */

#import <AppKit/NSDisplay.h>
#import <AppKit/NSRaise.h>
#import <AppKit/NSColorList.h>
#import <AppKit/NSFontTypeface.h>
#import <Onyx2D/O2Font.h>

#define MENU_BAR_HEIGHT 24  // exclude from visibleFrame height

@implementation NSDisplay

+(void)initialize {
   printf("[NSDisplay +initialize] PID %d...\n", getpid());
   fflush(stdout);
   if(self==[NSDisplay class]){
    NSDictionary *map=[NSDictionary dictionaryWithObjectsAndKeys:
     @"Command",@"LeftControl",
     @"Alt",@"LeftAlt",
     @"Control",@"RightControl",
     @"Alt",@"RightAlt",
     nil];
    NSDictionary *modifierMapping=[NSDictionary dictionaryWithObject:map forKey:@"NSModifierFlagMapping"];

    [[NSUserDefaults standardUserDefaults] registerDefaults:modifierMapping];
   }
   printf("[NSDisplay +initialize] done PID %d\n", getpid());
   fflush(stdout);
}

+(NSDisplay *)currentDisplay {
   printf("[NSDisplay +currentDisplay] PID %d calling NSThreadSharedInstance...\n", getpid());
   fflush(stdout);
   NSDisplay *d = NSThreadSharedInstance(@"NSDisplay");
   printf("[NSDisplay +currentDisplay] PID %d got display %p\n", getpid(), d);
   fflush(stdout);
   return d;
}

-init {
    printf("[NSDisplay -init] Initializing display for PID %d...\n", getpid());
    fflush(stdout);
    _eventQueue=[NSMutableArray new];
    _screens = [NSMutableArray new];

    CGDirectDisplayID cgDisplays[8];
    uint32_t count = 0;
    printf("[NSDisplay -init] Calling CGMainDisplayID()...\n");
    fflush(stdout);
    CGDirectDisplayID mainDisplay = CGMainDisplayID();
    printf("[NSDisplay -init] CGMainDisplayID returned 0x%x, getting active display list...\n", mainDisplay);
    fflush(stdout);
    CGGetActiveDisplayList(8, &cgDisplays, &count);
    printf("[NSDisplay -init] active display count=%u, copying mode...\n", count);
    fflush(stdout);

    // make the main display first in our screen list
    // the main display is the one that has an origin of 0,0
    printf("[NSDisplay -init] calling CGDisplayCopyDisplayMode...\n"); fflush(stdout);
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(mainDisplay);
    printf("[NSDisplay -init] got mode=%p (w=%zu h=%zu), calling CGDisplayCopyColorSpace...\n",
           mode, mode ? CGDisplayModeGetWidth(mode) : 0, mode ? CGDisplayModeGetHeight(mode) : 0); fflush(stdout);
    CGColorSpaceRef cs = CGDisplayCopyColorSpace(mainDisplay);
    printf("[NSDisplay -init] got cs=%p\n", cs); fflush(stdout);
    NSRect frame = NSMakeRect(0, 0, CGDisplayModeGetWidth(mode), CGDisplayModeGetHeight(mode));
    NSRect visFrame = frame;
    visFrame.size.height -= MENU_BAR_HEIGHT;
    printf("[NSDisplay -init] allocating NSScreen frame=(%f,%f,%f,%f)...\n",
           frame.origin.x, frame.origin.y, frame.size.width, frame.size.height); fflush(stdout);
    NSScreen *screen = [[[NSScreen alloc] initWithFrame:frame visibleFrame:visFrame] retain];
    printf("[NSDisplay -init] screen=%p, calling _propertiesFromMode...\n", screen); fflush(stdout);
    [screen _propertiesFromMode:mode colorSpace:cs displayID:mainDisplay];
    printf("[NSDisplay -init] _propertiesFromMode done\n"); fflush(stdout);
    CGDisplayModeRelease(mode);
    [_screens addObject:screen];

    // now add any other displays as additional screens
    for(int i = 0; i < count; ++i) {
        if(cgDisplays[i] == mainDisplay)
            continue;
        mode = CGDisplayCopyDisplayMode(cgDisplays[i]);
        cs = CGDisplayCopyColorSpace(mainDisplay);
        frame = NSMakeRect(0, 0, CGDisplayModeGetWidth(mode), CGDisplayModeGetHeight(mode));
        NSScreen *screen = [[[NSScreen alloc] initWithFrame:frame visibleFrame:frame] retain];
        [screen _propertiesFromMode:mode colorSpace:cs displayID:cgDisplays[i]];
        CGDisplayModeRelease(mode);
        [_screens addObject:screen];
    }
    
    _depth = 32;
    printf("[NSDisplay -init] Finished initializing display (screens=%lu)!\n", (unsigned long)[_screens count]);
    fflush(stdout);
    return self;
}

-(NSArray *)screens {
    return [NSArray arrayWithArray:_screens];
}

-(NSScreen *)mainScreen {
    if ([_screens count] > 0)
        return [_screens objectAtIndex:0];
    return nil;
}

-(BOOL)isReady {
    return YES;
}

-(uint32_t)depth { return _depth; }

-(NSPasteboard *)pasteboardWithName:(NSString *)name {
   NSUnimplementedMethod();
   return nil;
}

-(NSDraggingManager *)draggingManager {
   NSUnimplementedMethod();
   return nil;
}

-(NSColor *)colorWithName:(NSString *)colorName {
   if([colorName isEqual:@"controlColor"])
      return [NSColor lightGrayColor];
   if([colorName isEqual:@"disabledControlTextColor"])
      return [NSColor grayColor];
   if([colorName isEqual:@"controlTextColor"])
      return [NSColor blackColor];
   if([colorName isEqual:@"menuBackgroundColor"])
      return [NSColor lightGrayColor];
   if([colorName isEqual:@"controlShadowColor"])
      return [NSColor darkGrayColor];
   if([colorName isEqual:@"selectedControlColor"])
      return [NSColor cyanColor];
   if([colorName isEqual:@"controlBackgroundColor"])
      return [NSColor whiteColor];
   if([colorName isEqual:@"controlLightHighlightColor"])
      return [NSColor lightGrayColor];

   if([colorName isEqual:@"textBackgroundColor"])
      return [NSColor whiteColor];
   if([colorName isEqual:@"textColor"])
      return [NSColor blackColor];
   if([colorName isEqual:@"menuItemTextColor"])
      return [NSColor blackColor];
   if([colorName isEqual:@"selectedMenuItemTextColor"])
      return [NSColor blackColor];
   if([colorName isEqual:@"selectedMenuItemColor"])
      return [NSColor cyanColor];
   if([colorName isEqual:@"selectedControlTextColor"])
      return [NSColor blackColor];
   
   return nil;
}

-(void)_addSystemColor:(NSColor *) result forName:(NSString *)colorName {
   NSUnimplementedMethod();
 }


-(NSTimeInterval)textCaretBlinkInterval {
   return 0.5;
}

-(void)hideCursor {
   NSUnimplementedMethod();
}

-(void)unhideCursor {
   NSUnimplementedMethod();
}

// Arrow, IBeam, HorizontalResize, VerticalResize
-(id)cursorWithName:(NSString *)name {
   NSUnimplementedMethod();
   return nil;
}

-(void)setCursor:(id)cursor {
   NSUnimplementedMethod();
}

-(NSEvent *)nextEventMatchingMask:(unsigned)mask untilDate:(NSDate *)untilDate inMode:(NSString *)mode dequeue:(BOOL)dequeue {
    NSEvent *result=nil;

    if([_eventQueue count])
        untilDate=[NSDate date];
   
    [[NSRunLoop currentRunLoop] addInputSource:[NSApp inputSource] forMode:mode];
    [[NSRunLoop currentRunLoop] runMode:mode beforeDate:untilDate];
    [NSApp _drainPipe]; // if there are events, read one at a time
    [[NSRunLoop currentRunLoop] removeInputSource:[NSApp inputSource] forMode:mode];

    while(result==nil && [_eventQueue count]>0) {
        NSEvent *check=[_eventQueue objectAtIndex:0];
    
    if(!(NSEventMaskFromType([check type])&mask))
         [_eventQueue removeObjectAtIndex:0];
    else {
         result=[[check retain] autorelease];

         NSEventType _type = [result type];
         if(_type == NSMouseMoved || _type == NSLeftMouseDragged || _type == NSRightMouseDragged)
             pointerPos = [result locationInWindow];

        if(dequeue)
            [_eventQueue removeObjectAtIndex:0];
       }
   }

    if(result==nil)
        result=[[[NSEvent alloc] initWithType:NSAppKitSystem location:NSMakePoint(0,0) modifierFlags:0 window:nil] autorelease];
   
    return result;
}

-(void)discardEventsMatchingMask:(unsigned)mask beforeEvent:(NSEvent *)event {
   int count=[_eventQueue count];

   while(--count>=0){
    NSEvent *check=[_eventQueue objectAtIndex:count];

    if(check==event)
     break;
   }

   while(--count>=0){
    if(NSEventMaskFromType([event type])&mask)
     [_eventQueue removeObjectAtIndex:count];
   }
}

-(void)postEvent:(NSEvent *)event atStart:(BOOL)atStart {
   if(atStart)
    [_eventQueue insertObject:event atIndex:0];
   else
    [_eventQueue addObject:event];
}

-(BOOL)containsAndRemovePeriodicEvents {
   BOOL result=NO;
   int  count=[_eventQueue count];

   while(--count>=0){
    if([(NSEvent *)[_eventQueue objectAtIndex:count] type]==NSPeriodic){
     result=YES;
     [_eventQueue removeObjectAtIndex:count];
    }
   }

   return result;
}

-(unsigned)modifierForDefault:(NSString *)key:(unsigned)standard {
   NSDictionary *modmap=[[NSUserDefaults standardUserDefaults] dictionaryForKey:@"NSModifierFlagMapping"];
   NSString     *remap=[modmap objectForKey:key];

   if([remap isEqualToString:@"Command"])
    return NSCommandKeyMask;
   if([remap isEqualToString:@"Alt"])
    return NSAlternateKeyMask;
   if([remap isEqualToString:@"Control"])
    return NSControlKeyMask;

   return standard;
}

-(void)beep {
   NSUnimplementedMethod();
}

-(NSSet *)allFontFamilyNames {
   return [NSSet setWithObjects:@"Helvetica", @"Courier", @"Times", @"Lucida Grande", @"Geneva", @"Monaco", nil];
}

-(NSArray *)fontTypefacesForFamilyName:(NSString *)familyName {
   NSFontTypeface *face = [[[NSFontTypeface alloc] initWithName:familyName traitName:@"Regular" traits:0] autorelease];
   return [NSArray arrayWithObject:face];
}

-(float)scrollerWidth {
   return 15.0;
}

-(float)doubleClickInterval {
   return 1.0;
}


-(int)runModalPageLayoutWithPrintInfo:(NSPrintInfo *)printInfo {
   NSUnimplementedMethod();
	return 0;
}

-(int)runModalPrintPanelWithPrintInfoDictionary:(NSMutableDictionary *)attributes {
   NSUnimplementedMethod();
   return 0;
}

-(O2Context *)graphicsPortForPrintOperationWithView:(NSView *)view printInfo:(NSPrintInfo *)printInfo pageRange:(NSRange)pageRange {
   NSUnimplementedMethod();
   return nil;
}

-(int)savePanel:(NSSavePanel *)savePanel runModalForDirectory:(NSString *)directory file:(NSString *)file {
   NSUnimplementedMethod();
   return 0;
}

-(int)openPanel:(NSOpenPanel *)openPanel runModalForDirectory:(NSString *)directory file:(NSString *)file types:(NSArray *)types {
   NSUnimplementedMethod();
   return 0;
}

-(NSPoint)mouseLocation {
    return pointerPos;
}

-(NSUInteger)currentModifierFlags {
   NSUnimplementedMethod();
   return 0;
}

@end

void NSColorSetCatalogColor(NSString *catalogName,NSString *colorName,NSColor *color){
    NSColorList *list = [NSColorList colorListNamed:catalogName];
    if(list)
        [list setColor:color forKey:colorName];
}

NSColor *NSColorGetCatalogColor(NSString *catalogName,NSString *colorName){
    return [NSColor colorWithCatalogName:catalogName colorName:colorName];
}

#import <AppKit/NSGraphicsStyle.h>

@implementation NSGraphicsStyle (Overrides) 
-(void)drawMenuBranchArrowInRect:(NSRect)rect selected:(BOOL)selected {
    NSImage* arrow=[NSImage imageNamed:@"NSMenuArrow"];
    // ??? magic numbers
    rect.origin.y+=5;
    rect.origin.x-=2;
    [arrow drawInRect:rect fromRect:NSZeroRect operation:NSCompositeSourceOver fraction:1.0];
}
@end
