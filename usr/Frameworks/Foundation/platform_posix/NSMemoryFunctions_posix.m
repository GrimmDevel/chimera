/* Copyright (c) 2006-2007 Christopher J. W. Lloyd
   Copyright (C) 2024 Zoe Knox <zoe@ravynsoft.com>

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

#ifdef PLATFORM_IS_POSIX
#import <Foundation/NSPlatform.h>
#import <Foundation/NSDebug.h>
#import <Foundation/NSZombieObject.h>
#import <Foundation/NSString.h>
#import <Foundation/NSThread.h>
#import <Foundation/NSAutoreleasePool.h>
#import <Foundation/NSError.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

// some notes:
// - this uses POSIX thread local storage functions
// - there is no zone support (deprecated in 64 bit macOS)
// also corrected behavior of NSAllocateMemoryPages to be page-aligned


NSUInteger NSPageSize(void) {
    return 4096;
}

void *NSAllocateMemoryPages(NSUInteger byteCount) {
    size_t bytes = NSRoundUpToMultipleOfPageSize(byteCount);
    void *buffer = calloc(bytes, 1);
    if (buffer == NULL) {
        //fprintf(stderr, "NSAllocateMemoryPages(%u) failed. Error: %s\n", byteCount, strerror(errno));
        [NSException raise:NSInvalidArgumentException format:@"size: %u error: %s", bytes, strerror(errno)];
    }
    return buffer;
}

void NSDeallocateMemoryPages(void *pointer,NSUInteger byteCount) {
   free(pointer);
}

void NSCopyMemoryPages(const void *src,void *dst,NSUInteger byteCount) {
   const uint8_t *srcb=src;
   uint8_t       *dstb=dst;
   NSUInteger     i;

   for(i=0;i<byteCount;i++)
    dstb[i]=srcb[i];
}

NSZone *NSCreateZone(NSUInteger startSize,NSUInteger granularity,BOOL canFree){
   return NULL;
}

NSZone *NSDefaultMallocZone(void){
   return NULL;
}

void NSRecycleZone(NSZone *zone) {
}

void NSSetZoneName(NSZone *zone,NSString *name){
}

NSString *NSZoneName(NSZone *zone) {
   return @"zone";
}

NSZone *NSZoneFromPointer(void *pointer){
   return NULL;
}

void *NSZoneCalloc(NSZone *zone,NSUInteger numElems,NSUInteger numBytes){
    if (numElems == 0 || numBytes == 0) {
        return malloc(16);
    }
    void *buffer = calloc(numElems,numBytes);
    if (buffer == NULL) {
        fprintf(stderr, "NSZoneCalloc(zone, %lu, %lu) failed. Error: %s\n", (unsigned long)numElems, (unsigned long)numBytes, strerror(errno));
    }
    return buffer;
}

void NSZoneFree(NSZone *zone,void *pointer){
   free(pointer);
}

void *NSZoneMalloc(NSZone *zone,NSUInteger size){
    if (size > 64 * 1024 * 1024) {
        fprintf(stderr, "[NSZoneMalloc PID %d] REJECTING BOGUS SIZE %lu bytes (callers: %p %p %p %p)\n",
            getpid(), (unsigned long)size,
            __builtin_return_address(0),
            __builtin_return_address(1),
            __builtin_return_address(2),
            __builtin_return_address(3));
        return NULL;
    }
    if (size == 0) size = 16;
    void *buffer = malloc(size);
    if (buffer == NULL) {
        fprintf(stderr, "NSZoneMalloc(zone, %lu) failed. Error: %s\n", (unsigned long)size, strerror(errno));
    }
    return buffer;
}

void *NSZoneRealloc(NSZone *zone,void *pointer,NSUInteger size){
    if (size > 64 * 1024 * 1024) {
        fprintf(stderr, "[NSZoneRealloc PID %d] REJECTING BOGUS SIZE %lu bytes\n", getpid(), (unsigned long)size);
        return NULL;
    }
    if (size == 0) size = 16;
    void *buffer = realloc(pointer, size);
    if (buffer == NULL && size > 0) {
        fprintf(stderr, "NSZoneRealloc(zone, %p, %lu) failed. Error: %s\n", pointer, (unsigned long)size, strerror(errno));
    }
    return buffer;
}

static pthread_key_t _NSThreadInstanceKey() {
	static pthread_key_t key = -1;	
	if (key == -1) 
	{
		if (pthread_key_create(&key, NULL) != 0)
			[NSException raise:NSInternalInconsistencyException format:@"pthread_key_create failed"];
	}

	return key;
}

void NSPlatformSetCurrentThread(NSThread *thread) {
	pthread_setspecific(_NSThreadInstanceKey(), thread);
}

NSThread *NSPlatformCurrentThread() {
    pthread_key_t k = _NSThreadInstanceKey();
	NSThread *thread = pthread_getspecific(k);
	
	if(!thread)
	{
        printf("[NSPlatformCurrentThread PID %d] thread is NULL for key %d, initializing...\n", getpid(), (int)k);
        fflush(stdout);
		// maybe NSThread is not +initialize'd
		[NSThread class];
		thread=pthread_getspecific(k);
        if(!thread) {
            printf("[NSPlatformCurrentThread PID %d] allocating new NSThread...\n", getpid());
            fflush(stdout);
            thread = [NSThread alloc];
            if(thread) {
                NSPlatformSetCurrentThread(thread);
                {
                    NSAutoreleasePool *pool = [NSAutoreleasePool new];
                    [thread init];
                    [pool release];
                }
            }
        }        
		if(!thread)
		{
            printf("[NSPlatformCurrentThread PID %d] FATAL: No current thread\n", getpid());
            fflush(stdout);
			[NSException raise:NSInternalInconsistencyException format:@"No current thread"];
		}
        printf("[NSPlatformCurrentThread PID %d] initialized current thread %p\n", getpid(), thread);
        fflush(stdout);
	}
	
	return thread;
}

NSUInteger NSPlatformDetachThread(void *(*func)(void *arg), void *arg, NSError **errorp) {
	pthread_t thread;
    int err;
	if ((err = pthread_create(&thread, NULL, func, arg)) != 0) {
        if (errorp) *errorp = [NSError errorWithDomain:NSPOSIXErrorDomain code:err userInfo:nil];
        return 0;
    }
	return (NSUInteger)thread;
}
#endif
