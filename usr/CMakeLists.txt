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
)

# Custom link command for static Mach-O executables via Clang / Apple ld
set(CMAKE_C_LINK_EXECUTABLE 
    "${CMAKE_C_COMPILER} --target=x86_64-apple-darwin -static -nostdlib -Wl,-e,_start -Wl,-pagezero_size,0x1000 <OBJECTS> -o <TARGET> <LINK_LIBRARIES> <LINK_FLAGS>")

# 1. C runtime entry and libsystem_xiu
add_library(xiu_crt0 OBJECT
    libsystem/crt0.S
)

add_library(system_xiu STATIC
    libsystem/libsystem_xiu.c
    libsystem/gui.c
    libsystem/syscalls.S
    libsystem/setjmp.S
)

target_include_directories(system_xiu PUBLIC
    ${CMAKE_SOURCE_DIR}/kernel/include
    ${CMAKE_SOURCE_DIR}/usr/libsystem
    ${CMAKE_SOURCE_DIR}/usr/libsystem/include
)

function(xiu_add_user_crt target)
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:xiu_crt0>)
endfunction()

# 2. Shell (sh)
add_executable(sh bin/sh.c)
xiu_add_user_crt(sh)
target_link_libraries(sh system_xiu)

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

add_executable(wserver bin/wserver.c)
xiu_add_user_crt(wserver)
target_link_libraries(wserver system_xiu)

add_executable(guiapp bin/guiapp.c)
xiu_add_user_crt(guiapp)
target_link_libraries(guiapp system_xiu)

add_executable(calc bin/calc.c)
xiu_add_user_crt(calc)
target_link_libraries(calc system_xiu)

add_executable(kilo ${CMAKE_SOURCE_DIR}/kilo/kilo.c)
xiu_add_user_crt(kilo)
target_link_libraries(kilo system_xiu)

# 4. dash Shell Port
set(DASH_SRC_DIR ${CMAKE_SOURCE_DIR}/dash/src)
file(GLOB DASH_SOURCES ${DASH_SRC_DIR}/*.c)

list(REMOVE_ITEM DASH_SOURCES 
    ${DASH_SRC_DIR}/mkinit.c 
    ${DASH_SRC_DIR}/mknodes.c 
    ${DASH_SRC_DIR}/mksignames.c 
    ${DASH_SRC_DIR}/mksyntax.c
)

add_executable(dash ${DASH_SOURCES}
    ${DASH_SRC_DIR}/builtins.c
    ${DASH_SRC_DIR}/nodes.c
    ${DASH_SRC_DIR}/syntax.c
    ${DASH_SRC_DIR}/signames.c
    ${DASH_SRC_DIR}/init.c
    ${DASH_SRC_DIR}/bltin/printf.c
    ${DASH_SRC_DIR}/bltin/test.c
    ${DASH_SRC_DIR}/bltin/times.c
)
xiu_add_user_crt(dash)
target_link_libraries(dash system_xiu)
target_include_directories(dash PRIVATE ${DASH_SRC_DIR})
target_compile_definitions(dash PRIVATE -DHAVE_CONFIG_H -DBSD -D_GNU_SOURCE -DUSE_TEE=0 -DHAVE_KILLPG=1 -DSHELL -DHAVE_BSEARCH=1 -DHAVE_STRTOD=1)

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
    -DONE_SOURCE=1
    -DCONFIG_TCCDIR=\"/lib/tcc\"
    -DCONFIG_SYSROOT=\"\"
    -DCONFIG_TCC_CRTPREFIX=\"/lib\"
    -DCONFIG_TCC_ELFINTERP=\"\"
    -DCONFIG_TCC_LIBPATHS=\"/lib:/usr/lib\"
    -DCONFIG_TCC_SYSINCLUDEPATHS=\"/usr/include\"
    -DTCC_VERSION=\"0.9.27\"
)
