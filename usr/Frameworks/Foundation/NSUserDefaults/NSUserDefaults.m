/* Copyright (c) 2006-2007 Christopher J. W. Lloyd

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

#import <Foundation/NSUserDefaults.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSBundle.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#import <Foundation/NSThread.h>
#import <Foundation/NSProcessInfo.h>
#import <Foundation/NSRaise.h>
#import <Foundation/NSThread-Private.h>
#import <Foundation/NSPlatform.h>
#import <Foundation/NSPersistantDomain.h>
#import <Foundation/NSRaiseException.h>
#import <langinfo.h>

NSString * const NSGlobalDomain=@"NSGlobalDomain";
NSString * const NSArgumentDomain=@"NSArgumentDomain";
NSString * const NSRegistrationDomain=@"NSRegistrationDomain";

NSString * const NSMonthNameArray=@"NSMonthNameArray";
NSString * const NSWeekDayNameArray=@"NSWeekDayNameArray";
NSString * const NSTimeFormatString=@"NSTimeFormatString";
NSString * const NSDateFormatString=@"NSDateFormatString";
NSString * const NSAMPMDesignation=@"NSAMPMDesignation";
NSString * const NSTimeDateFormatString=@"NSTimeDateFormatString";

NSString * const NSShortWeekDayNameArray=@"NSShortWeekDayNameArray";
NSString * const NSShortMonthNameArray=@"NSShortMonthNameArray";

NSString * const NSUserDefaultsDidChangeNotification=@"NSUserDefaultsDidChangeNotification";

@implementation NSUserDefaults

-(void)registerArgumentDefaults {
   NSMutableDictionary *reg=[NSMutableDictionary dictionary];
   NSArray             *args=[[NSProcessInfo processInfo] arguments];
   NSInteger                  i,count=[args count];

   for(i=1;i<count-1;i+=2){
    NSString *key=[args objectAtIndex:i];
    NSString *val=[args objectAtIndex:i+1];
    NSString *pval;

    if([key length]==0 || [key characterAtIndex:0]!='-')
     break;

    key=[key substringFromIndex:1];

    NS_DURING
     if((pval=[val propertyList])==nil)
      pval=val;
      
    NS_HANDLER
     pval=val;
    NS_ENDHANDLER

    [reg setObject:pval forKey:key];
   }

   [_domains setObject:reg forKey:NSArgumentDomain];
}

-(void)registerFoundationDefaults {
   NSString     *path=[[NSBundle bundleForClass:[self class]] 
                  pathForResource:@"NSUserDefaults" ofType:@"plist"];
   if(path!=nil){
       NSDictionary *plist=[NSDictionary dictionaryWithContentsOfFile:path];
       if(plist!=nil)
           [_domains setObject:plist forKey:@"Foundation"];
   }
}

-(void)registerProcessNameDefaults {
  NSString           *name=[[NSBundle mainBundle] bundleIdentifier];
  id domain = nil;
  
  if(name==nil)
   name=[NSString stringWithFormat:@"noid.%@",[[NSProcessInfo processInfo] processName]];
   
  Class pClass = [[NSPlatform currentPlatform] persistantDomainClass];
  if (pClass) {
      domain = [pClass persistantDomainWithName:name];
  }
  if (domain == nil) {
      domain = [NSMutableDictionary dictionary];
  }

  NSString *pname = [[NSProcessInfo processInfo] processName];
  if (pname && domain) {
      [_domains setObject:domain forKey:pname];
  }
}

-init {
   _domains=[NSMutableDictionary new];
   _searchList=[[NSArray allocWithZone:NULL] initWithObjects:
      NSArgumentDomain,
      [[NSProcessInfo processInfo] processName],
      NSGlobalDomain,
      NSRegistrationDomain,
     @"Foundation",
      nil];

   [[NSProcessInfo processInfo] environment];
   
   [self registerFoundationDefaults];

   [self registerArgumentDefaults];
   [self registerProcessNameDefaults];

    NSMutableDictionary *dict = [NSMutableDictionary new];
    NSDictionary *globalDom = [_domains objectForKey:NSGlobalDomain];
    if (globalDom) {
        [dict addEntriesFromDictionary:globalDom];
    }
    [_domains setObject:dict forKey:NSGlobalDomain];
    
    const char *lstr = [[[[NSLocale currentLocale] localeIdentifier]
        stringByAppendingString:@".UTF-8"] UTF8String];
    char *current = setlocale(LC_ALL, NULL);
    if (lstr) setlocale(LC_ALL, lstr);
    
    // long day names
    NSMutableArray *arr = [NSMutableArray arrayWithCapacity:7];
    for (int d = DAY_1; d <= DAY_7; d++) {
        const char *s = nl_langinfo(d);
        NSString *str = (s && s[0]) ? [NSString stringWithUTF8String:s] : nil;
        [arr addObject:str ? str : @"Day"];
    }
    [dict setObject:arr forKey:NSWeekDayNameArray];

    // short day names
    arr = [NSMutableArray arrayWithCapacity:7];
    for (int d = ABDAY_1; d <= ABDAY_7; d++) {
        const char *s = nl_langinfo(d);
        NSString *str = (s && s[0]) ? [NSString stringWithUTF8String:s] : nil;
        [arr addObject:str ? str : @"D"];
    }
    [dict setObject:arr forKey:NSShortWeekDayNameArray];

    // long month names
    arr = [NSMutableArray arrayWithCapacity:12];
    for (int m = MON_1; m <= MON_12; m++) {
        const char *s = nl_langinfo(m);
        NSString *str = (s && s[0]) ? [NSString stringWithUTF8String:s] : nil;
        [arr addObject:str ? str : @"Month"];
    }
    [dict setObject:arr forKey:NSMonthNameArray];

    // short month names
    arr = [NSMutableArray arrayWithCapacity:12];
    for (int m = ABMON_1; m <= ABMON_12; m++) {
        const char *s = nl_langinfo(m);
        NSString *str = (s && s[0]) ? [NSString stringWithUTF8String:s] : nil;
        [arr addObject:str ? str : @"M"];
    }
    [dict setObject:arr forKey:NSShortMonthNameArray];

    const char *tfmt = nl_langinfo(T_FMT);
    NSString *tstr = (tfmt && tfmt[0]) ? [NSString stringWithUTF8String:tfmt] : nil;
    [dict setObject:tstr ? tstr : @"%H:%M:%S"
        forKey:NSTimeFormatString];

    const char *dfmt = nl_langinfo(D_FMT);
    NSString *dstr = (dfmt && dfmt[0]) ? [NSString stringWithUTF8String:dfmt] : nil;
    [dict setObject:dstr ? dstr : @"%m/%d/%y"
        forKey:NSDateFormatString];

    const char *dtfmt = nl_langinfo(D_T_FMT);
    NSString *dtstr = (dtfmt && dtfmt[0]) ? [NSString stringWithUTF8String:dtfmt] : nil;
    [dict setObject:dtstr ? dtstr : @"%a %b %e %H:%M:%S %Y"
        forKey:NSTimeDateFormatString];

    arr = [NSMutableArray arrayWithCapacity:2];
    const char *am = nl_langinfo(AM_STR);
    const char *pm = nl_langinfo(PM_STR);
    NSString *amStr = (am && am[0]) ? [NSString stringWithUTF8String:am] : nil;
    NSString *pmStr = (pm && pm[0]) ? [NSString stringWithUTF8String:pm] : nil;
    [arr addObject:amStr ? amStr : @"AM"];
    [arr addObject:pmStr ? pmStr : @"PM"];
    [dict setObject:arr forKey:NSAMPMDesignation];

    if (current) setlocale(LC_ALL, current);

   [_domains setObject:[NSMutableDictionary dictionary]
                forKey:NSRegistrationDomain];

   return self;
}

-initWithUser:(NSString *)user {
   NSUnimplementedMethod();
   return nil;
}

NSUserDefaults* stdUserDefaults = nil;

+(NSUserDefaults *)standardUserDefaults {
	@synchronized(self) {
		if (nil == stdUserDefaults) {
			stdUserDefaults = [[NSUserDefaults alloc] init];
		}
	}
	return stdUserDefaults;
}

+(void)resetStandardUserDefaults {
   NSUnimplementedMethod();
}

+ (BOOL)standardUserDefaultsAvailable
{
    return stdUserDefaults != nil;
}

-(void)addSuiteNamed:(NSString *)name {
   NSUnimplementedMethod();
}

-(void)removeSuiteNamed:(NSString *)name {
   NSUnimplementedMethod();
}

-(NSArray *)searchList {
   return _searchList;
}

-(void)setSearchList:(NSArray *)array {
	@synchronized(self) {
		[array retain];
	   [_searchList release];
	   _searchList=array;
	}
}

-(NSDictionary *)_buildDictionaryRep {
	NSMutableDictionary *result=[NSMutableDictionary dictionary];
	@synchronized(self) {
	   NSInteger                  i,count=[_searchList count];

	   for(i=0;i<count;i++){
		NSDictionary *domain=[_domains objectForKey:[_searchList objectAtIndex:i]];
		NSEnumerator *state=[domain keyEnumerator];
		id            key;

		while((key=[state nextObject])!=nil){
		 id value=[domain objectForKey:key];

	// NSPersistantDomain may return nil, addEntriesFromDictionary doesn't do that test
		 if(value!=nil)
		  [result setObject:value forKey:key];
		}
	   }
	}
   return result;
}

-(NSDictionary *)dictionaryRepresentation {
	NSDictionary* dictRep = nil;
	@synchronized(self) {
	   if(_dictionaryRep==nil)
		_dictionaryRep=[[self _buildDictionaryRep] retain];

		dictRep = _dictionaryRep;
	}
	return dictRep;
}

-(void)registerDefaults:(NSDictionary *)values {
	@synchronized(self) {
		[[_domains objectForKey:NSRegistrationDomain] addEntriesFromDictionary:values];
	}
}


-(NSArray *)volatileDomainNames {
   NSUnimplementedMethod();
   return nil;
}

-(NSArray *)persistentDomainNames {
    return [NSArray arrayWithObject:[[NSProcessInfo processInfo] processName]];
}

-(NSDictionary *)volatileDomainForName:(NSString *)name {
   NSUnimplementedMethod();
   return nil;
}

-(NSDictionary *)persistentDomainForName:(NSString *)name {
   NSMutableDictionary   *result=[NSMutableDictionary dictionary];
   NSPersistantDomain    *domain=[[[NSPlatform currentPlatform] persistantDomainClass] persistantDomainWithName:name];
   NSArray               *allKeys=[domain allKeys];
   NSInteger                    i,count=[allKeys count];

   for(i=0;i<count;i++){
    NSString *key=[allKeys objectAtIndex:i];
       id value = [domain objectForKey: key];
       if (value) {
           [result setObject:value forKey:key];
       }
   }

   return result;
}

-(void)setVolatileDomain:(NSDictionary *)domain
  forName:(NSString *)name {
   NSUnimplementedMethod();
}

-(void)setPersistentDomain:(NSDictionary *)domain
   forName:(NSString *)name {
   NSUnimplementedMethod();
}


-(void)removeVolatileDomainForName:(NSString *)name {
   NSUnimplementedMethod();
}

-(void)removePersistentDomainForName:(NSString *)name {
   NSUnimplementedMethod();
}

-(BOOL)synchronize {
   NSMutableDictionary *p = [self persistantDomain];
   if(p)
      [p synchronize];
   return 0;
}

-(NSMutableDictionary *)persistantDomain {
	NSMutableDictionary *dict = nil;
	@synchronized(self) {
		dict = [_domains objectForKey:[[NSProcessInfo processInfo] processName]];
		[[dict retain] autorelease];
	}
	return dict;
}

-objectForKey:(NSString *)defaultName {
	@synchronized(self) {
	   NSInteger i,count=[_searchList count];

	   for(i=0;i<count;i++){
		NSDictionary *domain=[_domains objectForKey:[_searchList objectAtIndex:i]];
		id            object=[domain objectForKey:defaultName];

		if(object!=nil)
		 return object;
	   }
	}
   return nil;
}

-(NSData *)dataForKey:(NSString *)defaultName {
   NSData *data=[self objectForKey:defaultName];

   return [data isKindOfClass:objc_lookUpClass("NSData")]?data:(NSData *)nil;
}

-(NSString *)stringForKey:(NSString *)defaultName {
   NSString *string=[self objectForKey:defaultName];

   return [string isKindOfClass:objc_lookUpClass("NSString")]?string:(NSString *)nil;
}

-(NSArray *)arrayForKey:(NSString *)defaultName {
   NSArray *array=[self objectForKey:defaultName];

   return [array isKindOfClass:objc_lookUpClass("NSArray")]?array:(NSArray *)nil;
}


-(NSDictionary *)dictionaryForKey:(NSString *)defaultName {
   NSDictionary *dictionary=[self objectForKey:defaultName];

   return [dictionary isKindOfClass:objc_lookUpClass("NSDictionary")]?dictionary:(NSDictionary *)nil;
}

-(NSArray *)stringArrayForKey:(NSString *)defaultName {
   NSArray *array=[self objectForKey:defaultName];
   NSInteger      count;

   if(![array isKindOfClass:objc_lookUpClass("NSArray")])
    return nil;

   count=[array count];
   while(--count>=0)
    if(![[array objectAtIndex:count] isKindOfClass:objc_lookUpClass("NSString")])
     return nil;

   return array;
}


-(BOOL)boolForKey:(NSString *)defaultName {
   id object=[self objectForKey:defaultName];

   if([object isKindOfClass:[NSNumber class]] || [object isKindOfClass:[NSString class]])
    return [object boolValue];
   
   return NO;
}

-(NSInteger)integerForKey:(NSString *)defaultName {
   id number=[self objectForKey:defaultName];

   return [number isKindOfClass:objc_lookUpClass("NSString")]?[(NSString *)number intValue]:
     ([number isKindOfClass:objc_lookUpClass("NSNumber")]?[(NSNumber *)number intValue]:0);
}


-(float)floatForKey:(NSString *)defaultName {
   id number=[self objectForKey:defaultName];

   return [number isKindOfClass:objc_lookUpClass("NSString")]?[(NSString *)number floatValue]:
     ([number isKindOfClass:objc_lookUpClass("NSNumber")]?[(NSNumber *)number floatValue]:0.0);

}

-(double)doubleForKey:(NSString *)defaultName {
	id number=[self objectForKey:defaultName];
	
	return [number isKindOfClass:objc_lookUpClass("NSString")]?[(NSString *)number doubleValue]:
	([number isKindOfClass:objc_lookUpClass("NSNumber")]?[(NSNumber *)number doubleValue]:0.0);
	
}

-(void)setObject:value forKey:(NSString *)key {
	@synchronized(self) {
        // We'll remove from the persistant domain the values that are equal to the registered one
        // Cocoa does that - even if the method documentation says nothing about it
        if ([value isEqual:[[_domains objectForKey:NSRegistrationDomain] objectForKey:key]]) {
            value = nil;
        }
        if (value) {
            [[self persistantDomain] setObject:value forKey:key];
        } else {
            [[self persistantDomain] removeObjectForKey:key];
        }
	   [_dictionaryRep autorelease];
	   _dictionaryRep=nil;
	   
	   [[NSNotificationCenter defaultCenter] postNotificationName:NSUserDefaultsDidChangeNotification object:self];
	}
}

-(void)setBool:(BOOL)value forKey:(NSString *)defaultName {
   [self setObject:value?@"YES":@"NO" forKey:defaultName];
}

-(void)setInteger:(NSInteger)value forKey:(NSString *)defaultName {
   [self setObject:[NSNumber numberWithInteger:value] forKey:defaultName];
}

-(void)setFloat:(float)value forKey:(NSString *)defaultName {
   [self setObject:[NSNumber numberWithFloat:value] forKey:defaultName];
}

-(void)setDouble:(double)value forKey:(NSString *)defaultName {
	[self setObject:[NSNumber numberWithDouble:value] forKey:defaultName];
}

-(void)removeObjectForKey:(NSString *)key {
	@synchronized(self) {
	   [[self persistantDomain] removeObjectForKey:key];
	   
	   [[NSNotificationCenter defaultCenter] postNotificationName:NSUserDefaultsDidChangeNotification object:self];
	}
}

-(BOOL)objectIsForcedForKey:(NSString *)key {
   NSUnimplementedMethod();
   return 0;
}

-(BOOL)objectIsForcedForKey:(NSString *)key inDomain:(NSString *)domain {
   NSUnimplementedMethod();
   return 0;
}

@end
