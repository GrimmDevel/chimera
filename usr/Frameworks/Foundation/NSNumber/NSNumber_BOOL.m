/* Copyright (c) 2006-2007 Christopher J. W. Lloyd

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */
#import "NSNumber_BOOL.h"
#import <Foundation/NSStringFormatter.h>

static NSNumber_BOOL *sTrue = nil;
static NSNumber_BOOL *sFalse = nil;

const CFBooleanRef kCFBooleanTrue = (CFBooleanRef)&sTrue;
const CFBooleanRef kCFBooleanFalse = (CFBooleanRef)&sFalse;

@implementation NSNumber_BOOL

NSNumber *NSNumber_BOOLNew(NSZone *zone,BOOL value) {
   if (!sTrue) {
       sTrue = (NSNumber_BOOL *)NSAllocateObject([NSNumber_BOOL class], 0, NULL);
       sTrue->_value = YES;
       sTrue->_type = kCFNumberCharType;
       sFalse = (NSNumber_BOOL *)NSAllocateObject([NSNumber_BOOL class], 0, NULL);
       sFalse->_value = NO;
       sFalse->_type = kCFNumberCharType;
   }
   return value ? sTrue : sFalse;
}

+ (id) allocWithZone:(NSZone *)zone {
   return [self alloc];
}

// Being constant singletons (doubletons?), boolean numbers can't be released.
-(void)dealloc {
   return;
   [super dealloc];  // Silence compiler warning
}

-(id)retain {
   return self;
}


- (oneway void)release
{
}


-(id)autorelease {
   return self;
}

-(NSUInteger)retainCount {
   /* "For objects that never get released (that is, their release method
      does nothing), this method should return UINT_MAX, as defined in
      <limits.h>." -- NSObject Protocol Reference
   */
   return UINT_MAX;
}

-(void)getValue:(void *)value {
   *((BOOL *)value)=_value;
}

-(const char *)objCType {
   return @encode(BOOL);
}

-(char)charValue {
   return _value;
}

-(unsigned char)unsignedCharValue {
   return _value;
}

-(short)shortValue {
   return _value;
}

-(unsigned short)unsignedShortValue {
   return _value;
}

-(int)intValue {
   return _value;
}

-(unsigned int)unsignedIntValue {
   return _value;
}

-(long)longValue {
   return _value;
}

-(unsigned long)unsignedLongValue {
   return _value;
}

-(long long)longLongValue {
   return _value;
}

-(unsigned long long)unsignedLongLongValue {
   return _value;
}

-(float)floatValue {
   return _value;
}

-(double)doubleValue {
   return _value;
}

-(BOOL)boolValue {
   return _value;
}

-(NSInteger)integerValue {
   return (NSInteger)_value;
}

-(NSUInteger)unsignedIntegerValue {
   return (NSUInteger)_value;
}

-(NSString *)descriptionWithLocale:(NSDictionary *)locale {
   return NSStringWithFormatAndLocale(@"%i",locale,_value);
}

@end

#import <Foundation/NSCFTypeID.h>

@implementation NSNumber_BOOL (CFTypeID)

- (unsigned) _cfTypeID
{
   return kNSCFTypeBoolean;
}

@end
