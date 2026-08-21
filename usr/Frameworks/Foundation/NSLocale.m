/* Copyright (c) 2006-2007 Christopher J. W. Lloyd

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */
#import <Foundation/NSDictionary.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSRaise.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSArray.h>

#include <locale.h>

NSString * const NSLocaleCountryCode=@"NSLocaleCountryCode";
NSString * const NSLocaleLanguageCode=@"NSLocaleLanguageCode";
NSString * const NSLocaleVariantCode=@"NSLocaleVariantCode";
NSString * const NSLocaleIdentifier=@"NSLocaleIdentifier";

NSString * const NSLocaleGroupingSeparator=@"NSLocaleGroupingSeparator";
NSString * const NSLocaleDecimalSeparator=@"NSLocaleDecimalSeparator";
NSString * const NSLocaleCalendar=@"NSLocaleCalendar";
NSString * const NSLocaleCurrencyCode=@"NSLocaleCurrencyCode";
NSString * const NSLocaleCurrencySymbol=@"NSLocaleCurrencySymbol";
NSString * const NSLocaleUsesMetricSystem=@"NSLocaleUsesMetricSystem";
NSString * const NSLocaleMeasurementSystem=@"NSLocaleMeasurementSystem";

NSString * const NSLocaleScriptCode=@"NSLocaleScriptCode";
NSString * const NSLocaleExemplarCharacterSet=@"NSLocaleExemplarCharacterSet";
NSString * const NSLocaleCollationIdentifier=@"NSLocaleCollationIdentifier";

NSString * const NSCurrentLocaleDidChangeNotification=@"NSCurrentLocaleDidChangeNotification";

BOOL NSCurrentLocaleIsMetric();

@implementation NSLocale

static NSLocale *_sharedSystemLocale  = nil;
static NSLocale *_sharedCurrentLocale = nil;

-(NSDictionary *)_locale {
   return _locale;
}

+systemLocale {
   if (_sharedSystemLocale == nil)
      _sharedSystemLocale = [[NSLocale alloc] initWithLocaleIdentifier:@"en_US"];
   return _sharedSystemLocale;
}

+currentLocale {
   if (_sharedCurrentLocale == nil)
   {
      NSString *localeIdentifier;
      
      if([self respondsToSelector:@selector(_platformCurrentLocaleIdentifier)])
       localeIdentifier=[self performSelector:@selector(_platformCurrentLocaleIdentifier)];
      else
       localeIdentifier=@"en_US";
       
      _sharedCurrentLocale = [[NSLocale alloc] initWithLocaleIdentifier:localeIdentifier];
   }
   return _sharedCurrentLocale;
}

-(void)dealloc {
   [_locale release];
   [super dealloc];
}

+autoupdatingCurrentLocale {
   NSUnimplementedMethod();
   return 0;
}

+(NSArray *)availableLocaleIdentifiers {
   NSUnimplementedMethod();
   return 0;
}

+(NSString *)canonicalLocaleIdentifierFromString:(NSString *)string {
   NSUnimplementedMethod();
   return 0;
}

+(NSDictionary *)componentsFromLocaleIdentifier:(NSString *)identifier {
   if ([identifier isEqualToString:[[NSLocale currentLocale] localeIdentifier]])
      return [_sharedCurrentLocale _locale];
   else if ([identifier isEqualToString:[[NSLocale systemLocale] localeIdentifier]])
      return [_sharedSystemLocale _locale];
   else
      return [[[[NSLocale alloc] initWithLocaleIdentifier:identifier] autorelease] _locale];
   return 0;
}

+(NSString *)localeIdentifierFromComponents:(NSDictionary *)components {
   NSUnimplementedMethod();
   return 0;
}

+(NSArray *)ISOCountryCodes {
   NSUnimplementedMethod();
   return 0;
}

+(NSArray *)ISOLanguageCodes {
   NSUnimplementedMethod();
   return 0;
}

+(NSArray *)ISOCurrencyCodes {
   NSUnimplementedMethod();
   return 0;
}

+(NSArray *)commonISOCurrencyCodes {
   NSUnimplementedMethod();
   return 0;
}

+(NSArray *)preferredLanguages {
   NSUnimplementedMethod();
   return 0;
}

-initWithLocaleIdentifier:(NSString *)identifier {
   [super init];

    if (!identifier || [identifier length] == 0) {
        identifier = @"en_US";
    }

	NSArray *parts = [identifier componentsSeparatedByString:@"_"];
	NSString *language = (parts && [parts count] > 0) ? [parts objectAtIndex:0] : @"en";
	NSString *country = (parts && [parts count] > 1) ? [parts objectAtIndex:1] : identifier;

	NSMutableDictionary *localeInfo = [NSMutableDictionary dictionaryWithObjectsAndKeys:
									   identifier, NSLocaleIdentifier,
									   language, NSLocaleLanguageCode,
									   country, NSLocaleCountryCode,
									   nil
									   ];
	
	if([[self class] respondsToSelector:@selector(_platformLocaleAdditionalDescriptionForIdentifier:)]) {
		// Use any platform specific method to fill the locale info if one is defined
		NSDictionary *info = [[self class] performSelector:@selector(_platformLocaleAdditionalDescriptionForIdentifier:) withObject:identifier];
		if (info && [info isKindOfClass:[NSDictionary class]]) {
			[localeInfo addEntriesFromDictionary:info];
		}
	} else {
		// Else use setlocale & localeconv to try to get some locale info
		char *currentLocale = setlocale(LC_ALL, NULL);
		NSString *locWithUtf8 = [identifier stringByAppendingString:@".UTF-8"];
		if (setlocale(LC_ALL, [locWithUtf8 UTF8String]) == NULL) {
			identifier = @"en_US";
		}
		struct lconv *conv = localeconv();
		
		NSNumber *usesMetric=[NSNumber numberWithBool:NSCurrentLocaleIsMetric(identifier)];
		
		if (conv && conv->decimal_point) {
			NSString *s = [NSString stringWithUTF8String:conv->decimal_point];
			if (s) [localeInfo setObject:s forKey: NSLocaleDecimalSeparator];
		}
		if (conv && conv->currency_symbol) {
			NSString *s = [NSString stringWithUTF8String:conv->currency_symbol];
			if (s) [localeInfo setObject:s forKey: NSLocaleCurrencySymbol];
		}
		if (usesMetric) {
			[localeInfo setObject:usesMetric forKey: NSLocaleUsesMetricSystem];
		}
		
		// Restore the initial locale
		if (currentLocale) {
			setlocale(LC_ALL, currentLocale);
		}
	}
	_locale = [[NSDictionary allocWithZone:NULL] initWithDictionary:localeInfo];
	return self;
}

-(void)encodeWithCoder:(NSCoder *)coder {
   NSUnimplementedMethod();
}

-initWithCoder:(NSCoder *)coder {
	// A very basic implementation that handles Locale encoding in nib files
	NSDeallocateObject(self);
	return [[NSLocale systemLocale] retain];
}

-copyWithZone:(NSZone *)zone {
   return [self retain];
}

-(NSString *)localeIdentifier {
   return [_locale objectForKey:NSLocaleIdentifier];
}

-objectForKey:key {
   return [_locale objectForKey:key];
}

-(NSString *)displayNameForKey:key value:value {
   NSUnimplementedMethod();
   return 0;
}

-(NSString *)description
{
	return [NSString stringWithFormat:@"<%@:%p %@>", [self class], self, _locale];
}
@end
