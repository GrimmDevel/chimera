/* Copyright (c) 2006-2007 Christopher J. W. Lloyd
   Copyright (c) 2024-2026 Zoe Knox

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
SOFTWARE.*/

#import <Foundation/NSObject.h>
#import <Foundation/NSAutoreleasePool.h>
#import <Foundation/NSException.h>
#import <Foundation/NSHashTable.h>
#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSInvocation.h>
#import <Foundation/NSString.h>
#import <Foundation/NSAutoreleasePool-private.h>
#import <Foundation/NSMethodSignature.h>
#import <Foundation/NSProxy.h>
#import <Foundation/NSRaise.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import "forwarding.h"

#include <CoreFoundation/CFRuntime.h>

/* zoe 2/10/26 - we now use NSObject from Apple's runtime. The remaining functions
 * here are additions or changes to the base class */

#if __LITTLE_ENDIAN__
#define CF_INFO_BITS 0
#define CF_RC_BITS 3
#else
#define CF_INFO_BITS 3
#define CF_RC_BITS 0
#endif

typedef void *malloc_zone_t;

// From Apple docs:
// Returns a Boolean value that indicates whether the receiver is an instance of given class
// or an instance of any class that inherits from that class.

BOOL NSObjectIsKindOfClass(id object,Class kindOf) {
   Class class=object_getClass(object);

	while (object_getClass(object_getClass(class)) != class) {

		if(kindOf == class) {
			return YES;
		}
		
		class = class_getSuperclass(class);
	}
	
   return NO;
}


@interface NSInvocation(private)
+(NSInvocation *)invocationWithMethodSignature:(NSMethodSignature *)signature arguments:(void *)arguments;
@end


@implementation NSObject

+ (void)initialize {
}

+ (id)alloc {
    return [self allocWithZone:NULL];
}

+ (id)allocWithZone:(NSZone *)zone {
    return NSAllocateObject(self, 0, zone);
}

+ (id)new {
    return [[self alloc] init];
}

- (id)init {
    return self;
}

- (void)dealloc {
    NSDeallocateObject(self);
}

+ (Class)class {
    return self;
}

- (Class)class {
    return object_getClass(self);
}

+ (Class)superclass {
    return class_getSuperclass(self);
}

- (Class)superclass {
    return class_getSuperclass(object_getClass(self));
}

+ (BOOL)isKindOfClass:(Class)aClass {
    for (Class c = self; c != Nil; c = class_getSuperclass(c)) {
        if (c == aClass) return YES;
    }
    return NO;
}

- (BOOL)isKindOfClass:(Class)aClass {
    for (Class c = object_getClass(self); c != Nil; c = class_getSuperclass(c)) {
        if (c == aClass) return YES;
    }
    return NO;
}

+ (BOOL)isMemberOfClass:(Class)aClass {
    return self == aClass;
}

- (BOOL)isMemberOfClass:(Class)aClass {
    return object_getClass(self) == aClass;
}

+ (BOOL)respondsToSelector:(SEL)aSelector {
    return class_respondsToSelector(object_getClass((id)self), aSelector);
}

- (BOOL)respondsToSelector:(SEL)aSelector {
    return class_respondsToSelector(object_getClass(self), aSelector);
}

+ (IMP)methodForSelector:(SEL)aSelector {
    IMP imp = class_getMethodImplementation(object_getClass((id)self), aSelector);
    /* when self is a class and the selector names an instance method,
     * the metaclass walk above misses it; search the class itself */
    if (!imp)
        imp = class_getMethodImplementation(self, aSelector);
    return imp;
}

- (IMP)methodForSelector:(SEL)aSelector {
    return class_getMethodImplementation(object_getClass(self), aSelector);
}

+ (IMP)instanceMethodForSelector:(SEL)aSelector {
    return class_getMethodImplementation(self, aSelector);
}

- (IMP)instanceMethodForSelector:(SEL)aSelector {
    return class_getMethodImplementation(object_getClass(self), aSelector);
}

- (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector {
    Method method = class_getInstanceMethod(object_getClass(self), aSelector);
    if (method == NULL)
        return nil;
    return [NSMethodSignature signatureWithObjCTypes:method_getTypeEncoding(method)];
}

+ (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector {
    Method method = class_getInstanceMethod(self, aSelector);
    if (method == NULL)
        method = class_getClassMethod(self, aSelector);
    if (method == NULL)
        return nil;
    return [NSMethodSignature signatureWithObjCTypes:method_getTypeEncoding(method)];
}

- (id)performSelector:(SEL)aSelector {
    if (!aSelector) return nil;
    IMP imp = class_getMethodImplementation(object_getClass(self), aSelector);
    if (!imp) return nil;
    return ((id (*)(id, SEL))imp)(self, aSelector);
}

- (id)performSelector:(SEL)aSelector withObject:(id)object {
    if (!aSelector) return nil;
    IMP imp = class_getMethodImplementation(object_getClass(self), aSelector);
    if (!imp) return nil;
    return ((id (*)(id, SEL, id))imp)(self, aSelector, object);
}

- (id)performSelector:(SEL)aSelector withObject:(id)object1 withObject:(id)object2 {
    if (!aSelector) return nil;
    IMP imp = class_getMethodImplementation(object_getClass(self), aSelector);
    if (!imp) return nil;
    return ((id (*)(id, SEL, id, id))imp)(self, aSelector, object1, object2);
}

+ (id)performSelector:(SEL)aSelector {
    if (!aSelector) return nil;
    IMP imp = class_getMethodImplementation(object_getClass((id)self), aSelector);
    if (!imp) return nil;
    return ((id (*)(id, SEL))imp)((id)self, aSelector);
}

+ (id)performSelector:(SEL)aSelector withObject:(id)object {
    if (!aSelector) return nil;
    IMP imp = class_getMethodImplementation(object_getClass((id)self), aSelector);
    if (!imp) return nil;
    return ((id (*)(id, SEL, id))imp)((id)self, aSelector, object);
}

+ (id)performSelector:(SEL)aSelector withObject:(id)object1 withObject:(id)object2 {
    if (!aSelector) return nil;
    IMP imp = class_getMethodImplementation(object_getClass((id)self), aSelector);
    if (!imp) return nil;
    return ((id (*)(id, SEL, id, id))imp)((id)self, aSelector, object1, object2);
}

+ (BOOL)conformsToProtocol:(Protocol *)aProtocol {
    return class_conformsToProtocol(self, aProtocol);
}

- (BOOL)conformsToProtocol:(Protocol *)aProtocol {
    return class_conformsToProtocol(object_getClass(self), aProtocol);
}

- (id)retain {
    return self;
}

- (oneway void)release {
}

- (id)autorelease {
    return self;
}

- (NSUInteger)retainCount {
    return 1;
}

- (id)self {
    return self;
}

+ (id)self {
    return (id)self;
}

- (NSZone *)zone {
    return NSDefaultMallocZone();
}

- (NSUInteger)hash {
    return (NSUInteger)(uintptr_t)self;
}

- (BOOL)isEqual:(id)object {
    return self == object;
}

- (id)copy {
    return [(id<NSCopying>)self copyWithZone:NULL];
}

- (id)mutableCopy {
    return [(id<NSMutableCopying>)self mutableCopyWithZone:NULL];
}

- (NSString *)description {
    return [NSString stringWithFormat:@"<%@: %p>", NSStringFromClass([self class]), self];
}

+ (NSString *)description {
    return NSStringFromClass(self);
}

@end

@implementation NSObject (Foundation)

-(id)alloc {
    return NSAllocateObject(self, 0, NULL);
}

+(NSInteger)version {
   return class_getVersion(self);
}


+(void)setVersion:(NSInteger)version {
   class_setVersion(self,version);
}

+(void)initialize {
    //objc_setForwardHandler(_defaultForwardHandler,NULL);
}


+ (void)poseAsClass:(Class)aClass
{
    NSAutoreleasePool * pool = [NSAutoreleasePool new];
    NSUnimplementedMethod();
    [pool release];
}

-(uint32_t *)cfinfo {
   return (uint32_t *)(self->cfinfo);
}

-(Class)classForCoder {
   return object_getClass(self);
}

-(Class)classForArchiver {
   return [self classForCoder];
}

-(Class)classForKeyedArchiver {
	return [self classForCoder];
}

-replacementObjectForCoder:(NSCoder *)coder {
   return self;
}


-awakeAfterUsingCoder:(NSCoder *)coder {
   return self;
}

-(NSUInteger)_frameLengthForSelector:(SEL)selector {
   NSMethodSignature *signature=[self methodSignatureForSelector:selector];

   return [signature frameLength];
}

-(id)forwardSelector:(SEL)selector arguments:(void *)arguments {
   NSMethodSignature *signature=[self methodSignatureForSelector:selector];

   if(signature==nil){
    [self doesNotRecognizeSelector:selector];
    return nil;
   }
   else {
    NSInvocation *invocation=[NSInvocation invocationWithMethodSignature:signature arguments:arguments];
   // char          result[[signature methodReturnLength]];
    id              result;

    [self forwardInvocation:invocation];
    [invocation getReturnValue:&result];

   /* __builtin_return(result); */ //Can we use __builtin_return like this? It still doesn't seem to work on float/doubles ?
    return result;
   }
}

+(NSString *)className {
   return NSStringFromClass(self);
}

-(NSString *)className {
   return NSStringFromClass(object_getClass(self));
}

@end


#import <Foundation/NSCFTypeID.h>

@implementation NSObject (CFTypeID)

- (unsigned) _cfTypeID
{
   return kNSCFTypeObject;
}

@end
