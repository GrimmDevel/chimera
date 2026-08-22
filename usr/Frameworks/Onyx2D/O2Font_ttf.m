#import <Onyx2D/O2Font_ttf.h>
#import <Onyx2D/O2TTFDecoder.h>
#import <Onyx2D/O2DataProvider.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSDirectoryEnumerator.h>


@implementation O2Font_ttf

-initWithDataProvider:(O2DataProviderRef)provider {
   O2TTFDecoderRef decoder=O2TTFDecoderCreate(provider);

   _nameToGlyph=O2TTFDecoderGetPostScriptNameMapTable(decoder,&_numberOfGlyphs);
   _glyphLocations=O2TTFDecoderGetGlyphLocations(decoder,_numberOfGlyphs);
   return self;
}

-(O2Glyph)glyphWithGlyphName:(NSString *)name {
   return (O2Glyph)(int)NSMapGet(_nameToGlyph,name);
}

/* Search the standard font directories for name with a font extension,
 * comparing case-insensitively like the legacy font system did */
static NSString *pathForFontName(NSString *name) {
   static NSArray *fontDirs = nil;
   if (fontDirs == nil)
      fontDirs = [[NSArray alloc] initWithObjects:@"/System/Library/Fonts",
                                                     @"/Library/Fonts", nil];

   if (name == nil || [name length] == 0)
      return nil;

   NSString *lowerName = [[name lowercaseString] stringByAppendingString:@"."];

   for (NSUInteger d = 0; d < [fontDirs count]; d++) {
      NSString *dirPath = [fontDirs objectAtIndex:d];
      NSArray *contents = [[NSFileManager defaultManager]
                            contentsOfDirectoryAtPath:dirPath error:NULL];
      for (NSUInteger f = 0; f < [contents count]; f++) {
         NSString *fileName = [contents objectAtIndex:f];
         NSString *lowerFile = [fileName lowercaseString];
         if ([lowerFile isEqualToString:[name lowercaseString]] ||
             [lowerFile hasPrefix:lowerName]) {
            /* accept .ttf/.otf/.ttc collections as well */
            if ([lowerFile hasSuffix:@".ttf"] || [lowerFile hasSuffix:@".otf"] ||
                [lowerFile hasSuffix:@".ttc"] || [lowerFile hasSuffix:@".dfont"])
               return [dirPath stringByAppendingPathComponent:fileName];
         }
      }
   }

   return nil;
}

O2FontRef O2FontCreateWithFontName_platform(NSString *name) {
   NSString *path = pathForFontName(name);

   if (path == nil)
      return nil;

   O2DataProviderRef provider = O2DataProviderCreateWithURL(
      (NSURL *)[NSURL fileURLWithPath:path]);
   if (provider == NULL)
      return nil;

   return (O2FontRef)[(O2Font_ttf *)[O2Font_ttf alloc] initWithDataProvider:provider];
}

O2FontRef O2FontCreateWithDataProvider_platform(O2DataProviderRef provider) {
   return (O2FontRef)[(O2Font_ttf *)[O2Font_ttf alloc] initWithDataProvider:provider];
}

@end
