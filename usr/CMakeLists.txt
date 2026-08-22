# =============================================================================
# XIU Operating System — User-Space Build System (Native Mach-O 64-bit)
# usr/CMakeLists.txt
# =============================================================================

cmake_minimum_required(VERSION 3.20)
project(xiu_user)

# Configure compiler flags for Darwin Mach-O x86_64
add_compile_options(
    --target=x86_64-apple-darwin
    -static
    -nostdlib
    -fno-stack-protector
    -msse -msse2
    -DXIU_ARCH_x86_64=1
    -DXIU_PAGE_SIZE=4096
    -D_FORTIFY_SOURCE=0
    -DOS_OBJECT_USE_OBJC=0
)

# Custom link command for static Mach-O executables via Clang / Apple ld
set(CMAKE_C_LINK_EXECUTABLE 
    "${CMAKE_C_COMPILER} --target=x86_64-apple-darwin -static -nostdlib -Wl,-e,_start -Wl,-pagezero_size,0x1000 <OBJECTS> -o <TARGET> <LINK_LIBRARIES> <LINK_FLAGS>")

include_directories(
    libsystem/include
    libsystem/stdtime
    libsystem/locale
    libsystem/string
    libsystem/gen
    libsystem/stdio
)

# 1. C runtime entry and libsystem_xiu
add_library(xiu_crt0 OBJECT
    libsystem/crt0.S
)

add_library(system_xiu STATIC
    libsystem/libsystem_xiu.c
    libsystem/pwd_grp.c
    libsystem/getlogin.c
    libsystem/setlogin.c
    libsystem/pam_shim.c
    libsystem/stdtime/localtime.c
    libsystem/stdtime/strftime.c
    libsystem/stdtime/difftime.c
    libsystem/stdtime/asctime.c
    libsystem/stdtime/timelocal.c
    libsystem/stdio/vsscanf.c
    libsystem/locale/mbrtowc.c
    libsystem/locale/mbsrtowcs.c
    libsystem/locale/wcrtomb.c
    libsystem/locale/wcsrtombs.c
    libsystem/locale/utf8.c
    libsystem/locale/mblen.c
    libsystem/locale/mbsinit.c
    libsystem/locale/mbtowc.c
    libsystem/locale/wctob.c
    libsystem/locale/wctomb.c
    libsystem/locale/localeconv.c
    libsystem/locale/nl_langinfo.c
    libsystem/locale/setlocale.c


    libsystem/string/wcschr.c
    libsystem/string/wcscmp.c
    libsystem/string/wcslen.c
    libsystem/string/wmemchr.c
    libsystem/string/wcsncmp.c
    libsystem/string/wcscpy.c
    libsystem/string/wcscat.c
    libsystem/string/wcsdup.c
    libsystem/string/wcsstr.c
    libsystem/string/wmemcmp.c
    libsystem/string/wmemcpy.c
    libsystem/string/wmemmove.c
    libsystem/string/wmemset.c
    libsystem/string/strlcat.c
    libsystem/string/strcasecmp.c
    libsystem/string/strerror.c
    libsystem/string/strsignal.c
    libsystem/gen/errlst.c
    libsystem/gen/isatty.c


    libsystem/gen/uname.c
    libsystem/gen/gethostname.c
    libsystem/gen/sethostname.c
    libsystem/gen/getprogname.c
    libsystem/gen/setprogname.c
    libsystem/gen/err.c
    libsystem/gen/setmode.c
    libsystem/gen/times.c
    libsystem/gen/termios.c
    libcrypt/crypt.c
    libcrypt/misc.c
    libcrypt/crypt-sha256.c
    libcrypt/crypt-sha512.c
    libcrypt/crypt-md5.c
    libcrypt/crypt-nthash.c
    libcrypt/sha2.c
    libcrypt/md5.c
    libcrypt/md4.c
    libsystem/libinput_xiu.c
    libsystem/plist.c
    libsystem/gui.c
    libsystem/blocks/data.c
    libsystem/blocks/runtime.c
    libsystem/syscalls.S
    libsystem/setjmp.S
)


add_library(crypt STATIC
    libcrypt/crypt.c
    libcrypt/misc.c
    libcrypt/crypt-sha256.c
    libcrypt/crypt-sha512.c
    libcrypt/crypt-md5.c
    libcrypt/crypt-nthash.c
    libcrypt/sha2.c
    libcrypt/md5.c
    libcrypt/md4.c
)
target_include_directories(crypt PUBLIC
    ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    ${CMAKE_SOURCE_DIR}/usr/libcrypt/include
    ${CMAKE_SOURCE_DIR}/usr/libcrypt
)
target_compile_options(crypt PUBLIC
    -nostdinc
    -isystem ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    -I${CMAKE_SOURCE_DIR}/usr/libcrypt/include
    -I${CMAKE_SOURCE_DIR}/usr/libcrypt
    -Wno-incompatible-pointer-types-discards-qualifiers
    -Wno-pointer-sign
)

set(LIBOBJC_SRCS
    libobjc/objc_runtime_xiu.c
    libobjc/objc_msgSend_x86_64.S
)

add_library(objc OBJECT ${LIBOBJC_SRCS})
target_include_directories(objc PUBLIC
    ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    ${CMAKE_SOURCE_DIR}/usr/libobjc
    ${CMAKE_SOURCE_DIR}/usr/libobjc/runtime
)
target_compile_options(objc PUBLIC
    -nostdinc
    -isystem ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    -I${CMAKE_SOURCE_DIR}/usr/libobjc
    -I${CMAKE_SOURCE_DIR}/usr/libobjc/runtime
    -D__RAVYNOS__ -D__MACH__ -D__XIU__
    -DTARGET_OS_OSX=1 -DTARGET_OS_IOSMAC=0 -DTARGET_OS_MAC=1 -DTARGET_OS_IPHONE=0
    -Wno-error -Wno-undef-prefix -Wno-deprecated-declarations -Wno-unused-variable -Wno-macro-redefined
)

# -----------------------------------------------------------------------------
# Foundation Framework
# -----------------------------------------------------------------------------
# All Foundation sources; keep only XIU/posix platform files. Classes that
# must not be pulled in (external deps or conflicts with the runtime shims)
# are filtered out below.
file(GLOB_RECURSE FOUNDATION_SRCS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    Frameworks/Foundation/*.m
)
list(FILTER FOUNDATION_SRCS EXCLUDE REGEX "platform_(windows|solaris|linux|bsd|darwin)/")
list(FILTER FOUNDATION_SRCS EXCLUDE REGEX "NSConstantString")
list(FILTER FOUNDATION_SRCS EXCLUDE REGEX "objc_forward_ffi")
list(FILTER FOUNDATION_SRCS EXCLUDE REGEX "NSBlocks\\.m")
list(FILTER FOUNDATION_SRCS EXCLUDE REGEX "objc_size_alignment\\.m")
list(FILTER FOUNDATION_SRCS EXCLUDE REGEX "test_CFBridging")
# NSNetServices needs a bonjour helper that does not exist on XIU;
# NSURLCache needs CFUUID functions that are not implemented yet
list(FILTER FOUNDATION_SRCS EXCLUDE REGEX "NSNetService/")
list(FILTER FOUNDATION_SRCS EXCLUDE REGEX "NSURLCache\\.m")
# kCFBooleanTrue/False are already defined by NSNumber_BOOL.m
list(FILTER FOUNDATION_SRCS EXCLUDE REGEX "NSNumber_BOOL_const\\.m")

file(GLOB FOUNDATION_SUBDIRS ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/Foundation/*/)

add_library(Foundation OBJECT ${FOUNDATION_SRCS})
target_include_directories(Foundation PUBLIC
    ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    ${CMAKE_SOURCE_DIR}/usr/libsystem/blocks
    ${CMAKE_SOURCE_DIR}/usr/libobjc
    ${CMAKE_SOURCE_DIR}/usr/libobjc/runtime
    ${CMAKE_SOURCE_DIR}/usr/Frameworks
    ${CMAKE_SOURCE_DIR}/usr/Frameworks/Foundation
    ${CMAKE_SOURCE_DIR}/usr/Frameworks/Foundation/Headers
    ${FOUNDATION_SUBDIRS}
    ${CMAKE_SOURCE_DIR}/usr/Frameworks/CoreFoundation/Headers
)
target_compile_options(Foundation PUBLIC
    -x objective-c
    -nostdinc -isystem ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    -I${CMAKE_SOURCE_DIR}/usr/libsystem/blocks
    -I${CMAKE_SOURCE_DIR}/usr/libobjc
    -I${CMAKE_SOURCE_DIR}/usr/libobjc/runtime
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks/Foundation
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks/Foundation/Headers
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks/CoreFoundation/Headers
    -D__RAVYNOS__ -D__MACH__ -D__XIU__ -DPLATFORM_IS_POSIX=1 -DPLATFORM_USES_BSD_SOCKETS=1
    -DTARGET_OS_OSX=1 -DTARGET_OS_IOSMAC=0 -DTARGET_OS_MAC=1 -DTARGET_OS_IPHONE=0
    -Wno-error -Wno-undef-prefix -Wno-deprecated-declarations -Wno-unused-variable -Wno-macro-redefined
    -Wno-incompatible-pointer-types -Wno-implicit-function-declaration
)

# -----------------------------------------------------------------------------
# Onyx2D Graphics Framework (Quartz 2D)
# -----------------------------------------------------------------------------
set(ONYX2D_SRCS
    Frameworks/Onyx2D/O2AffineTransform.m
    Frameworks/Onyx2D/O2BitmapContext.m
    Frameworks/Onyx2D/O2ClipMask.m
    Frameworks/Onyx2D/O2ClipPhase.m
    Frameworks/Onyx2D/O2ClipState.m
    Frameworks/Onyx2D/O2Color.m
    Frameworks/Onyx2D/O2ColorSpace.m
    Frameworks/Onyx2D/O2Context.m
    Frameworks/Onyx2D/O2Context_builtin.m
    Frameworks/Onyx2D/O2DataConsumer.m
    Frameworks/Onyx2D/O2DataProvider.m
    Frameworks/Onyx2D/O2Encoding.m
    Frameworks/Onyx2D/O2Font.m
    Frameworks/Onyx2D/O2Geometry.m
    Frameworks/Onyx2D/O2GraphicsState.m
    Frameworks/Onyx2D/O2Image.m
    Frameworks/Onyx2D/O2Layer.m
    Frameworks/Onyx2D/O2MutablePath.m
    Frameworks/Onyx2D/O2Path.m
    Frameworks/Onyx2D/O2Pattern.m
    Frameworks/Onyx2D/O2Surface.m
    Frameworks/Onyx2D/VGPath.m
    Frameworks/Onyx2D/O2Paint.m
    Frameworks/Onyx2D/O2Paint_axialGradient.m
    Frameworks/Onyx2D/O2Paint_color.m
    Frameworks/Onyx2D/O2Paint_image.m
    Frameworks/Onyx2D/O2Paint_pattern.m
    Frameworks/Onyx2D/O2Paint_radialGradient.m
    Frameworks/Onyx2D/O2Paint_ramp.m
    Frameworks/Onyx2D/O2argb8u.m
    Frameworks/Onyx2D/O2argb32f.m
    Frameworks/Onyx2D/O2GlyphStencil.m
    Frameworks/Onyx2D/O2Function.m
)

add_library(Onyx2D OBJECT ${ONYX2D_SRCS})
target_include_directories(Onyx2D PUBLIC
    ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    ${CMAKE_SOURCE_DIR}/usr/libobjc
    ${CMAKE_SOURCE_DIR}/usr/libobjc/runtime
    ${CMAKE_SOURCE_DIR}/usr/Frameworks
    ${CMAKE_SOURCE_DIR}/usr/Frameworks/Foundation/Headers
    ${CMAKE_SOURCE_DIR}/usr/Frameworks/Onyx2D
)
target_compile_options(Onyx2D PUBLIC
    -x objective-c
    -nostdinc -isystem ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    -I${CMAKE_SOURCE_DIR}/usr/libobjc
    -I${CMAKE_SOURCE_DIR}/usr/libobjc/runtime
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks/Foundation/Headers
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks/Onyx2D
    -D__RAVYNOS__ -D__MACH__ -D__XIU__
    -DTARGET_OS_OSX=1 -DTARGET_OS_IOSMAC=0 -DTARGET_OS_MAC=1 -DTARGET_OS_IPHONE=0
    -Wno-error -Wno-undef-prefix -Wno-deprecated-declarations -Wno-unused-variable -Wno-macro-redefined
    -Wno-incompatible-pointer-types -Wno-implicit-function-declaration
)

# -----------------------------------------------------------------------------
# CoreGraphics Framework
# -----------------------------------------------------------------------------
set(COREGRAPHICS_SRCS
    Frameworks/CoreGraphics/CGAffineTransform.m
    Frameworks/CoreGraphics/CGBitmapContext.m
    Frameworks/CoreGraphics/CGColor.m
    Frameworks/CoreGraphics/CGColorSpace.m
    Frameworks/CoreGraphics/CGContext.m
    Frameworks/CoreGraphics/CGDataConsumer.m
    Frameworks/CoreGraphics/CGDataProvider.m
    Frameworks/CoreGraphics/CGDirectDisplay.m
    Frameworks/CoreGraphics/CGEvent.m
    Frameworks/CoreGraphics/CGFont.m
    Frameworks/CoreGraphics/CGFunction.m
    Frameworks/CoreGraphics/CGGeometry.m
    Frameworks/CoreGraphics/CGGradient.m
    Frameworks/CoreGraphics/CGImage.m
    Frameworks/CoreGraphics/CGLayer.m
    Frameworks/CoreGraphics/CGPath.m
    Frameworks/CoreGraphics/CGPattern.m
    Frameworks/CoreGraphics/CGWindow.m
)

add_library(CoreGraphics OBJECT ${COREGRAPHICS_SRCS})
target_include_directories(CoreGraphics PUBLIC
    ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    ${CMAKE_SOURCE_DIR}/usr/libobjc
    ${CMAKE_SOURCE_DIR}/usr/libobjc/runtime
    ${CMAKE_SOURCE_DIR}/usr/CoreServices
    ${CMAKE_SOURCE_DIR}/usr/Frameworks
    ${CMAKE_SOURCE_DIR}/usr/Frameworks/Foundation/Headers
    ${CMAKE_SOURCE_DIR}/usr/Frameworks/Onyx2D
    ${CMAKE_SOURCE_DIR}/usr/Frameworks/CoreGraphics
)
target_compile_options(CoreGraphics PUBLIC
    -x objective-c
    -nostdinc -isystem ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    -I${CMAKE_SOURCE_DIR}/usr/libobjc
    -I${CMAKE_SOURCE_DIR}/usr/libobjc/runtime
    -I${CMAKE_SOURCE_DIR}/usr/CoreServices
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks/Foundation/Headers
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks/Onyx2D
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks/CoreGraphics
    -D__RAVYNOS__ -D__MACH__ -D__XIU__
    -DTARGET_OS_OSX=1 -DTARGET_OS_IOSMAC=0 -DTARGET_OS_MAC=1 -DTARGET_OS_IPHONE=0
    -Wno-error -Wno-undef-prefix -Wno-deprecated-declarations -Wno-unused-variable -Wno-macro-redefined
    -Wno-incompatible-pointer-types -Wno-implicit-function-declaration
)

# -----------------------------------------------------------------------------
# AppKit Framework
# -----------------------------------------------------------------------------
# All AppKit sources except subprojects with external dependencies
file(GLOB_RECURSE APPKIT_SRCS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    Frameworks/AppKit/*.m
)
list(FILTER APPKIT_SRCS EXCLUDE REGEX "NSOpenGL/")
list(FILTER APPKIT_SRCS EXCLUDE REGEX "CoreData_")
list(FILTER APPKIT_SRCS EXCLUDE REGEX "_FT\\.m")
list(FILTER APPKIT_SRCS EXCLUDE REGEX "NSEvent_CoreGraphics\\.m")
# these pull in QuartzCore/CoreImage/PDF/CF symbols that don't exist on XIU yet
list(FILTER APPKIT_SRCS EXCLUDE REGEX "NSCIImageRep\\.m")
list(FILTER APPKIT_SRCS EXCLUDE REGEX "NSGradient\\.m")
list(FILTER APPKIT_SRCS EXCLUDE REGEX "NSPDFImageRep\\.m")
list(FILTER APPKIT_SRCS EXCLUDE REGEX "NSPrintOperation\\.m")
list(FILTER APPKIT_SRCS EXCLUDE REGEX "NSRichTextReader\\.m")
list(FILTER APPKIT_SRCS EXCLUDE REGEX "NSViewBackingLayer\\.m")

file(GLOB APPKIT_SUBDIRS ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/AppKit/*/)

add_library(AppKit OBJECT ${APPKIT_SRCS})
target_include_directories(AppKit PUBLIC
    ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    ${CMAKE_SOURCE_DIR}/usr/libobjc
    ${CMAKE_SOURCE_DIR}/usr/libobjc/runtime
    ${CMAKE_SOURCE_DIR}/usr/CoreServices
    ${CMAKE_SOURCE_DIR}/usr/Frameworks
    ${CMAKE_SOURCE_DIR}/usr/Frameworks/Foundation/Headers
    ${FOUNDATION_SUBDIRS}
    ${CMAKE_SOURCE_DIR}/usr/Frameworks/Onyx2D
    ${CMAKE_SOURCE_DIR}/usr/Frameworks/CoreGraphics
    ${CMAKE_SOURCE_DIR}/usr/Frameworks/AppKit
    ${APPKIT_SUBDIRS}
)
target_compile_options(AppKit PUBLIC
    -x objective-c
    -nostdinc -isystem ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    -I${CMAKE_SOURCE_DIR}/usr/libobjc
    -I${CMAKE_SOURCE_DIR}/usr/libobjc/runtime
    -I${CMAKE_SOURCE_DIR}/usr/CoreServices
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks/Foundation/Headers
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks/Onyx2D
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks/CoreGraphics
    -I${CMAKE_SOURCE_DIR}/usr/Frameworks/AppKit
    -D__RAVYNOS__ -D__MACH__ -D__XIU__
    -DTARGET_OS_OSX=1 -DTARGET_OS_IOSMAC=0 -DTARGET_OS_MAC=1 -DTARGET_OS_IPHONE=0
    -Wno-error -Wno-undef-prefix -Wno-deprecated-declarations -Wno-unused-variable -Wno-macro-redefined
    -Wno-deprecated-objc-isa-usage -Wno-incompatible-pointer-types -Wno-implicit-function-declaration
)

target_include_directories(system_xiu PUBLIC
    ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    ${CMAKE_SOURCE_DIR}/usr/libcrypt/include
    ${CMAKE_SOURCE_DIR}/usr/libcrypt
    ${CMAKE_SOURCE_DIR}/usr/libsystem
    ${CMAKE_SOURCE_DIR}/usr/libsystem/blocks
)
target_compile_options(system_xiu PUBLIC
    -nostdinc
    -isystem ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    -I${CMAKE_SOURCE_DIR}/usr/libcrypt/include
    -I${CMAKE_SOURCE_DIR}/usr/libcrypt
    -Wno-error -Wno-unused-variable -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-pointer-bool-conversion -Wno-pointer-sign
)

function(xiu_add_user_crt target)
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:xiu_crt0>)
endfunction()



# 3. Core Utilities
add_executable(ls bin/ls.c)
xiu_add_user_crt(ls)
target_link_libraries(ls system_xiu)

add_executable(cat bin/cat.c)
xiu_add_user_crt(cat)
target_link_libraries(cat system_xiu)

add_executable(echo bin/echo.c)
xiu_add_user_crt(echo)
target_link_libraries(echo system_xiu)

add_executable(mkdir bin/mkdir.c)
xiu_add_user_crt(mkdir)
target_link_libraries(mkdir system_xiu)

add_executable(rm bin/rm.c)
xiu_add_user_crt(rm)
target_link_libraries(rm system_xiu)

add_executable(touch bin/touch.c)
xiu_add_user_crt(touch)
target_link_libraries(touch system_xiu)

add_executable(pwd bin/pwd.c)
xiu_add_user_crt(pwd)
target_link_libraries(pwd system_xiu)

add_executable(neofetch bin/neofetch.c)
xiu_add_user_crt(neofetch)
target_link_libraries(neofetch system_xiu)

add_executable(proclist bin/proclist.c)
xiu_add_user_crt(proclist)
target_link_libraries(proclist system_xiu)

add_executable(ifconfig bin/ifconfig.c)
xiu_add_user_crt(ifconfig)
target_link_libraries(ifconfig system_xiu)

add_executable(ping bin/ping.c)
xiu_add_user_crt(ping)
target_link_libraries(ping system_xiu)

add_executable(nc bin/nc.c)
xiu_add_user_crt(nc)
target_link_libraries(nc system_xiu)

add_executable(curl bin/curl.c)
xiu_add_user_crt(curl)
target_link_libraries(curl system_xiu)

add_executable(machdemo bin/mach_demo.c)
xiu_add_user_crt(machdemo)
target_link_libraries(machdemo system_xiu)

add_executable(smpdemo bin/smp_demo.c)
xiu_add_user_crt(smpdemo)
target_link_libraries(smpdemo system_xiu)

set(XKBCOMMON_SRCS
    CoreServices/WindowServer/libxkbcommon/src/keysym.c
    CoreServices/WindowServer/libxkbcommon/src/context.c
    CoreServices/WindowServer/libxkbcommon/src/utils.c
    CoreServices/WindowServer/libxkbcommon/src/state.c
    CoreServices/WindowServer/libxkbcommon/src/keymap.c
    CoreServices/WindowServer/libxkbcommon/src/compose/paths.c
    CoreServices/WindowServer/libxkbcommon/src/compose/table.c
    CoreServices/WindowServer/libxkbcommon/src/compose/parser.c
    CoreServices/WindowServer/libxkbcommon/src/compose/state.c
    CoreServices/WindowServer/libxkbcommon/src/utf8.c
    CoreServices/WindowServer/libxkbcommon/src/context-priv.c
    CoreServices/WindowServer/libxkbcommon/src/keysym-utf.c
    CoreServices/WindowServer/libxkbcommon/src/atom.c
    CoreServices/WindowServer/libxkbcommon/src/util-list.c
    CoreServices/WindowServer/libxkbcommon/src/keymap-priv.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/parser.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/symbols.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/ast-build.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/action.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/xkbcomp.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/keymap.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/keycodes.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/types.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/keywords.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/rules.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/scanner.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/keymap-dump.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/include.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/expr.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/compat.c
    CoreServices/WindowServer/libxkbcommon/src/xkbcomp/vmod.c
    CoreServices/WindowServer/libxkbcommon/src/text.c
)

add_library(xkbcommon STATIC ${XKBCOMMON_SRCS})
target_compile_options(xkbcommon PRIVATE
    -nostdinc -isystem ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    -I${CMAKE_SOURCE_DIR}/usr/CoreServices/WindowServer/libxkbcommon/include
    -I${CMAKE_SOURCE_DIR}/usr/CoreServices/WindowServer/libxkbcommon/src
    -I${CMAKE_SOURCE_DIR}/usr/CoreServices/WindowServer/libxkbcommon/src/xkbcomp
    -I${CMAKE_SOURCE_DIR}/usr/CoreServices/WindowServer/libxkbcommon/src/compose
    -DHAVE_STRNDUP -DHAVE_UNISTD_H -DHAVE_ASPRINTF -DHAVE_SYS_SYSCTL_H -D_GNU_SOURCE
    -DDFLT_XKB_CONFIG_ROOT=\"/System/Library/KeyboardLayout\"
    -DDFLT_XKB_CONFIG_EXTRA_PATH=\"/Library/KeyboardLayout\"
    -DXLOCALEDIR=\"/System/Library/Locale\"
    -DDEFAULT_XKB_RULES=\"evdev\"
    -Wno-macro-redefined -Wno-unused-variable -Wno-incompatible-pointer-types
)
target_compile_definitions(xkbcommon PRIVATE
    DEFAULT_XKB_MODEL="pc105"
    DEFAULT_XKB_LAYOUT="us"
    DEFAULT_XKB_VARIANT=NULL
    DEFAULT_XKB_OPTIONS=NULL
)

set_source_files_properties(
    CoreServices/WindowServer/main.m
    CoreServices/WindowServer/WindowServer.m
    CoreServices/WindowServer/BSDFramebuffer.m
    CoreServices/WindowServer/WSDisplay.m
    CoreServices/WindowServer/WSWindowRecord.m
    CoreServices/WindowServer/WSAppRecord.m
    CoreServices/WindowServer/WSInput.m
    CoreServices/WindowServer/SystemUIServer/SystemUIServer.m
    CoreServices/WindowServer/SystemUIServer/AppDelegate.m
    CoreServices/WindowServer/SystemUIServer/ClockView.m
    CoreServices/WindowServer/SystemUIServer/MenuView.m
    CoreServices/WindowServer/SystemUIServer/ExtrasView.m
    CoreServices/WindowServer/SystemUIServer/MenuBarWindow.m
    CoreServices/WindowServer/SystemUIServer/NSMenuWindow.m
    CoreServices/WindowServer/SystemUIServer/NSMainMenuView.m
    CoreServices/WindowServer/SystemUIServer/SubmenuView.m
    CoreServices/WindowServer/SystemUIServer/Label.m
    CoreServices/WindowServer/SystemUIServer/AboutWindow.m
    CoreServices/Dock/main.m
    CoreServices/Dock/Dock.m
    CoreServices/Dock/DockItem.m
    CoreServices/Dock/DesktopWindow.m
    CoreServices/Dock/DockTileData.m
    CoreServices/Filer/FilerMain.m
    CoreServices/WindowServer/ws_font.c
    PROPERTIES
    LANGUAGE C
    COMPILE_FLAGS "-x objective-c -D__RAVYNOS__ -D__MACH__ -D__WSRPC -D__XIU__ -DRELEASE_LOGO=@\\\"ravynos-mark-64.png\\\" -nostdinc -isystem ${CMAKE_SOURCE_DIR}/usr/libsystem/include -I${CMAKE_SOURCE_DIR}/usr/CoreServices -I${CMAKE_SOURCE_DIR}/usr/CoreServices/WindowServer -I${CMAKE_SOURCE_DIR}/usr/CoreServices/WindowServer/SystemUIServer -I${CMAKE_SOURCE_DIR}/usr/CoreServices/Dock -I${CMAKE_SOURCE_DIR}/usr/CoreServices/WindowServer/libxkbcommon/include -I${CMAKE_SOURCE_DIR}/usr/Frameworks -I${CMAKE_SOURCE_DIR}/usr/Frameworks/Foundation/Headers -I${CMAKE_SOURCE_DIR}/usr/Frameworks/AppKit -I${CMAKE_SOURCE_DIR}/usr/Frameworks/CoreGraphics -I${CMAKE_SOURCE_DIR}/usr/Frameworks/Onyx2D -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-implicit-function-declaration -Wno-visibility -Wno-deprecated-declarations"
)

add_executable(WindowServer
    CoreServices/WindowServer/main.m
    CoreServices/WindowServer/WindowServer.m
    CoreServices/WindowServer/BSDFramebuffer.m
    CoreServices/WindowServer/WSDisplay.m
    CoreServices/WindowServer/WSWindowRecord.m
    CoreServices/WindowServer/WSAppRecord.m
    CoreServices/WindowServer/WSInput.m
    CoreServices/WindowServer/ws_font.c
)
xiu_add_user_crt(WindowServer)
target_link_libraries(WindowServer xkbcommon AppKit CoreGraphics Onyx2D Foundation objc system_xiu)

add_executable(SystemUIServer
    CoreServices/WindowServer/SystemUIServer/SystemUIServer.m
    CoreServices/WindowServer/SystemUIServer/AppDelegate.m
    CoreServices/WindowServer/SystemUIServer/ClockView.m
    CoreServices/WindowServer/SystemUIServer/MenuView.m
    CoreServices/WindowServer/SystemUIServer/ExtrasView.m
    CoreServices/WindowServer/SystemUIServer/MenuBarWindow.m
    CoreServices/WindowServer/SystemUIServer/NSMenuWindow.m
    CoreServices/WindowServer/SystemUIServer/NSMainMenuView.m
    CoreServices/WindowServer/SystemUIServer/SubmenuView.m
    CoreServices/WindowServer/SystemUIServer/Label.m
    CoreServices/WindowServer/SystemUIServer/AboutWindow.m
)
xiu_add_user_crt(SystemUIServer)
target_link_libraries(SystemUIServer AppKit CoreGraphics Onyx2D Foundation objc system_xiu)

add_executable(Dock
    CoreServices/Dock/main.m
    CoreServices/Dock/Dock.m
    CoreServices/Dock/DockItem.m
    CoreServices/Dock/DesktopWindow.m
    CoreServices/Dock/DockTileData.m
)
xiu_add_user_crt(Dock)
target_link_libraries(Dock AppKit CoreGraphics Onyx2D Foundation objc system_xiu)

add_executable(Filer
    CoreServices/Filer/FilerMain.m
)
xiu_add_user_crt(Filer)
target_link_libraries(Filer AppKit CoreGraphics Onyx2D Foundation objc system_xiu)

add_executable(guiapp bin/guiapp.c)
xiu_add_user_crt(guiapp)
target_link_libraries(guiapp AppKit CoreGraphics Onyx2D Foundation objc system_xiu)

add_executable(calc bin/calc.c)
xiu_add_user_crt(calc)
target_link_libraries(calc AppKit CoreGraphics Onyx2D Foundation objc system_xiu)

add_executable(plutil bin/plutil.c)
xiu_add_user_crt(plutil)
target_link_libraries(plutil system_xiu)

add_executable(defaults bin/defaults.c)
xiu_add_user_crt(defaults)
target_link_libraries(defaults system_xiu)

add_executable(cp bin/cp.c)
xiu_add_user_crt(cp)
target_link_libraries(cp system_xiu)

add_executable(mv bin/mv.c)
xiu_add_user_crt(mv)
target_link_libraries(mv system_xiu)

add_executable(head bin/head.c)
xiu_add_user_crt(head)
target_link_libraries(head system_xiu)

add_executable(tail bin/tail.c)
xiu_add_user_crt(tail)
target_link_libraries(tail system_xiu)

add_executable(grep bin/grep.c)
xiu_add_user_crt(grep)
target_link_libraries(grep system_xiu)

add_executable(wc bin/wc.c)
xiu_add_user_crt(wc)
target_link_libraries(wc system_xiu)

add_executable(chmod bin/chmod.c)
xiu_add_user_crt(chmod)
target_link_libraries(chmod system_xiu)

add_executable(uname bin/uname.c)
xiu_add_user_crt(uname)
target_link_libraries(uname system_xiu)

add_executable(date bin/date.c)
xiu_add_user_crt(date)
target_link_libraries(date system_xiu)

add_executable(env bin/env.c)
xiu_add_user_crt(env)
target_link_libraries(env system_xiu)

add_executable(whoami bin/whoami.c)
xiu_add_user_crt(whoami)
target_link_libraries(whoami system_xiu)

add_executable(login bin/login.c)
xiu_add_user_crt(login)
target_link_libraries(login system_xiu crypt)

add_executable(passwd bin/passwd_cmd.c)
xiu_add_user_crt(passwd)
target_link_libraries(passwd system_xiu crypt)

add_executable(su bin/su.c)
xiu_add_user_crt(su)
target_link_libraries(su system_xiu crypt)

add_executable(id bin/id.c)
xiu_add_user_crt(id)
target_link_libraries(id system_xiu)

add_executable(sleep bin/sleep.c)
xiu_add_user_crt(sleep)
target_link_libraries(sleep system_xiu)

add_executable(true bin/true.c)
xiu_add_user_crt(true)
target_link_libraries(true system_xiu)

add_executable(false bin/false.c)
xiu_add_user_crt(false)
target_link_libraries(false system_xiu)

add_executable(yes bin/yes.c)
xiu_add_user_crt(yes)
target_link_libraries(yes system_xiu)

add_executable(clear bin/clear.c)
xiu_add_user_crt(clear)
target_link_libraries(clear system_xiu)

add_executable(kill bin/kill.c)
xiu_add_user_crt(kill)
target_link_libraries(kill system_xiu)

add_executable(uptime bin/uptime.c)
xiu_add_user_crt(uptime)
target_link_libraries(uptime system_xiu)

add_executable(stat bin/stat.c)
xiu_add_user_crt(stat)
target_link_libraries(stat system_xiu)

add_executable(df bin/df.c)
xiu_add_user_crt(df)
target_link_libraries(df system_xiu)

add_executable(hexdump bin/hexdump.c)
xiu_add_user_crt(hexdump)
target_link_libraries(hexdump system_xiu)

add_executable(tee bin/tee.c)
xiu_add_user_crt(tee)
target_link_libraries(tee system_xiu)

add_executable(seq bin/seq.c)
xiu_add_user_crt(seq)
target_link_libraries(seq system_xiu)

add_executable(basename bin/basename.c)
xiu_add_user_crt(basename)
target_link_libraries(basename system_xiu)

add_executable(dirname bin/dirname.c)
xiu_add_user_crt(dirname)
target_link_libraries(dirname system_xiu)

add_executable(kilo ${CMAKE_SOURCE_DIR}/kilo/kilo.c)
xiu_add_user_crt(kilo)
target_link_libraries(kilo system_xiu)

add_executable(memload bin/memload.c)
xiu_add_user_crt(memload)
target_link_libraries(memload system_xiu)

# 4. FreeBSD / ravynOS BSD sh Port
set(SH_SRC_DIR ${CMAKE_SOURCE_DIR}/sh)
set(SH_SOURCES
    ${SH_SRC_DIR}/bltin/echo.c
    ${SH_SRC_DIR}/alias.c
    ${SH_SRC_DIR}/arith_yacc.c
    ${SH_SRC_DIR}/arith_yylex.c
    ${SH_SRC_DIR}/cd.c
    ${SH_SRC_DIR}/error.c
    ${SH_SRC_DIR}/eval.c
    ${SH_SRC_DIR}/exec.c
    ${SH_SRC_DIR}/expand.c
    ${SH_SRC_DIR}/histedit.c
    ${SH_SRC_DIR}/input.c
    ${SH_SRC_DIR}/jobs.c
    ${SH_SRC_DIR}/mail.c
    ${SH_SRC_DIR}/main.c
    ${SH_SRC_DIR}/memalloc.c
    ${SH_SRC_DIR}/miscbltin.c
    ${SH_SRC_DIR}/mystring.c
    ${SH_SRC_DIR}/options.c
    ${SH_SRC_DIR}/output.c
    ${SH_SRC_DIR}/parser.c
    ${SH_SRC_DIR}/redir.c
    ${SH_SRC_DIR}/show.c
    ${SH_SRC_DIR}/trap.c
    ${SH_SRC_DIR}/var.c
    ${SH_SRC_DIR}/builtins.c
    ${SH_SRC_DIR}/nodes.c
    ${SH_SRC_DIR}/syntax.c
    ${SH_SRC_DIR}/shims.c
)

add_executable(sh ${SH_SOURCES})
xiu_add_user_crt(sh)
target_link_libraries(sh system_xiu)
target_include_directories(sh PRIVATE ${SH_SRC_DIR} ${CMAKE_SOURCE_DIR}/usr/libsystem/include)
target_compile_definitions(sh PRIVATE -DHAVE_CONFIG_H -DBSD=1 -D_GNU_SOURCE -DUSE_TEE=0 -DHAVE_KILLPG=1 -DSHELL -DHAVE_BSEARCH=1 -DHAVE_STRTOD=1 -DNO_HISTORY=1)
target_compile_options(sh PRIVATE -Wno-int-conversion -Wno-implicit-function-declaration -Wno-unused-variable -Wno-nullability-completeness -Wno-everything)

# 5. Zsh Shell Port (ravynOS / Darwin)
set(ZSH_SRC_DIR ${CMAKE_SOURCE_DIR}/zsh/Src)
set(ZSH_SOURCES
    ${ZSH_SRC_DIR}/main.c
    ${ZSH_SRC_DIR}/init.c
    ${ZSH_SRC_DIR}/builtin.c
    ${ZSH_SRC_DIR}/compat.c
    ${ZSH_SRC_DIR}/cond.c
    ${ZSH_SRC_DIR}/context.c
    ${ZSH_SRC_DIR}/exec.c
    ${ZSH_SRC_DIR}/glob.c
    ${ZSH_SRC_DIR}/hashnameddir.c
    ${ZSH_SRC_DIR}/hashtable.c
    ${ZSH_SRC_DIR}/hist.c
    ${ZSH_SRC_DIR}/input.c
    ${ZSH_SRC_DIR}/jobs.c
    ${ZSH_SRC_DIR}/lex.c
    ${ZSH_SRC_DIR}/linklist.c
    ${ZSH_SRC_DIR}/loop.c
    ${ZSH_SRC_DIR}/math.c
    ${ZSH_SRC_DIR}/mem.c
    ${ZSH_SRC_DIR}/module.c
    ${ZSH_SRC_DIR}/options.c
    ${ZSH_SRC_DIR}/params.c
    ${ZSH_SRC_DIR}/parse.c
    ${ZSH_SRC_DIR}/pattern.c
    ${ZSH_SRC_DIR}/prompt.c
    ${ZSH_SRC_DIR}/signals.c
    ${ZSH_SRC_DIR}/signames.c
    ${ZSH_SRC_DIR}/sort.c
    ${ZSH_SRC_DIR}/string.c
    ${ZSH_SRC_DIR}/subst.c
    ${ZSH_SRC_DIR}/text.c
    ${ZSH_SRC_DIR}/utils.c
)

add_executable(zsh ${ZSH_SOURCES})
xiu_add_user_crt(zsh)
target_link_libraries(zsh system_xiu)
target_include_directories(zsh PRIVATE
    ${CMAKE_SOURCE_DIR}/zsh
    ${ZSH_SRC_DIR}
    ${ZSH_SRC_DIR}/Builtins
    ${CMAKE_SOURCE_DIR}/usr/libsystem/include
)
target_compile_definitions(zsh PRIVATE
    -DHAVE_CONFIG_H=1
    -DZSH_STATIC=1
    -D__MACH__=1
    -D__XIU__=1
    -D__APPLE__=1
)
target_compile_options(zsh PRIVATE
    -Wno-everything
)

# 6. stty Terminal Utility (ravynOS / BSD)
set(STTY_SRC_DIR ${CMAKE_SOURCE_DIR}/usr/bin/stty)
add_executable(stty
    ${STTY_SRC_DIR}/stty.c
    ${STTY_SRC_DIR}/cchar.c
    ${STTY_SRC_DIR}/gfmt.c
    ${STTY_SRC_DIR}/key.c
    ${STTY_SRC_DIR}/modes.c
    ${STTY_SRC_DIR}/print.c
    ${STTY_SRC_DIR}/util.c
)
xiu_add_user_crt(stty)
target_link_libraries(stty system_xiu)
target_include_directories(stty PRIVATE ${STTY_SRC_DIR} ${CMAKE_SOURCE_DIR}/usr/libsystem/include)
target_compile_definitions(stty PRIVATE -D__XIU__=1)
target_compile_options(stty PRIVATE -Wno-everything)


# 5. Tiny C Compiler (tcc) Port
set(TCC_SRC_DIR ${CMAKE_SOURCE_DIR}/tinycc)
add_executable(tcc ${TCC_SRC_DIR}/tcc.c)
xiu_add_user_crt(tcc)
target_link_libraries(tcc system_xiu)
target_include_directories(tcc PRIVATE
    ${TCC_SRC_DIR}
    ${CMAKE_SOURCE_DIR}/kernel/include
    ${CMAKE_SOURCE_DIR}/usr/libsystem
    ${CMAKE_SOURCE_DIR}/usr/libsystem/include
)
target_compile_definitions(tcc PRIVATE
    -DTCC_TARGET_X86_64=1
    -DTCC_TARGET_MACHO=1
    -DTCC_IS_NATIVE=1
    -DONE_SOURCE=1
    -DCONFIG_TCCDIR=\"/lib/tcc\"
    -DCONFIG_SYSROOT=\"\"
    -DCONFIG_TCC_CRTPREFIX=\"/lib\"
    -DCONFIG_TCC_LIBPATHS=\"/lib:/usr/lib\"
    -DCONFIG_TCC_SYSINCLUDEPATHS=\"/usr/include\"
    -DTCC_VERSION=\"0.9.27\"
)

# 7. Sudo Command (ravynOS / BSD Port)
set(SUDO_DIR ${CMAKE_SOURCE_DIR}/usr/sudo)
add_executable(sudo
    ${SUDO_DIR}/src/sudo.c
    ${SUDO_DIR}/src/parse_args.c
    ${SUDO_DIR}/src/conversation.c
    ${SUDO_DIR}/src/exec.c
    ${SUDO_DIR}/src/exec_common.c
    ${SUDO_DIR}/src/exec_nopty.c
    ${SUDO_DIR}/src/exec_pty.c
    ${SUDO_DIR}/src/get_pty.c
    ${SUDO_DIR}/src/hooks.c
    ${SUDO_DIR}/src/limits.c
    ${SUDO_DIR}/src/load_plugins.c
    ${SUDO_DIR}/src/net_ifs.c
    ${SUDO_DIR}/src/preserve_fds.c
    ${SUDO_DIR}/src/signal.c
    ${SUDO_DIR}/src/sudo_edit.c
    ${SUDO_DIR}/src/edit_open.c
    ${SUDO_DIR}/src/copy_file.c
    ${SUDO_DIR}/src/openbsd.c
    ${SUDO_DIR}/src/exec_iolog.c
    ${SUDO_DIR}/src/exec_intercept.c
    ${SUDO_DIR}/src/exec_monitor.c
    ${SUDO_DIR}/src/exec_ptrace.c
    ${SUDO_DIR}/src/suspend_parent.c
    ${SUDO_DIR}/src/tgetpass.c
    ${SUDO_DIR}/src/ttyname.c
    ${SUDO_DIR}/src/utmp.c
    ${SUDO_DIR}/lib/util/sudo_conf.c
    ${SUDO_DIR}/lib/util/sudo_debug.c
    ${SUDO_DIR}/lib/util/sudo_dso.c
    ${SUDO_DIR}/lib/util/event.c
    ${SUDO_DIR}/lib/util/event_select.c
    ${SUDO_DIR}/lib/util/fatal.c
    ${SUDO_DIR}/lib/util/getgrouplist.c
    ${SUDO_DIR}/lib/util/gethostname.c
    ${SUDO_DIR}/lib/util/gettime.c
    ${SUDO_DIR}/lib/util/basename.c
    ${SUDO_DIR}/lib/util/inet_ntop.c
    ${SUDO_DIR}/lib/util/memrchr.c
    ${SUDO_DIR}/lib/util/mktemp.c
    ${SUDO_DIR}/lib/util/pipe2.c
    ${SUDO_DIR}/lib/util/dup3.c
    ${SUDO_DIR}/lib/util/roundup.c
    ${SUDO_DIR}/lib/util/reallocarray.c
    ${SUDO_DIR}/lib/util/setgroups.c
    ${SUDO_DIR}/lib/util/sig2str.c
    ${SUDO_DIR}/lib/util/strtobool.c
    ${SUDO_DIR}/lib/util/term.c
    ${SUDO_DIR}/lib/util/arc4random.c
    ${SUDO_DIR}/lib/util/cfmakeraw.c
    ${SUDO_DIR}/lib/util/mkdirat.c
    ${SUDO_DIR}/lib/util/openat.c
    ${SUDO_DIR}/lib/util/getopt_long.c
    ${SUDO_DIR}/lib/util/getdelim.c
    ${SUDO_DIR}/lib/util/key_val.c
    ${SUDO_DIR}/lib/util/parseln.c
    ${SUDO_DIR}/lib/util/progname.c
    ${SUDO_DIR}/lib/util/strsplit.c
    ${SUDO_DIR}/lib/util/strtoid.c
    ${SUDO_DIR}/lib/util/strtomode.c
    ${SUDO_DIR}/lib/util/strtonum.c
    ${SUDO_DIR}/lib/util/ttysize.c
    ${SUDO_DIR}/lib/util/secure_path.c
    ${SUDO_DIR}/lib/util/lbuf.c
    ${SUDO_DIR}/lib/util/gidlist.c
    ${SUDO_DIR}/lib/util/locking.c
    ${SUDO_DIR}/lib/util/pw_dup.c
    ${SUDO_DIR}/lib/util/rcstr.c
)
xiu_add_user_crt(sudo)
target_link_libraries(sudo system_xiu)
target_include_directories(sudo PRIVATE
    ${CMAKE_SOURCE_DIR}/usr/libsystem/include
    ${SUDO_DIR}
    ${SUDO_DIR}/include
    ${SUDO_DIR}/src
)
target_compile_definitions(sudo PRIVATE
    -DHAVE_CONFIG_H=1
    -D_PATH_SUDO_CONF=\"/etc/sudo.conf\"
    -DLOCALEDIR=\"/usr/share/locale\"
    -D__MACH__=1
    -D__XIU__=1
    -D__APPLE__=1
    -D_DARWIN_C_SOURCE=1
    -DSTUB_LOAD_INTERFACES=1
)
target_compile_options(sudo PRIVATE -Wno-everything)

