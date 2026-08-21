# XIU Kernel — Core Subsystem Build
add_executable(xiu_kernel
    xiu_kernel_main.c
    console.c
    video_console.c
    iso_font.c
    lib.c
    kernel_stubs.c
    mach/ipc_port.c
    mach/ipc_kmsg.c
    mach/ipc_kobject.c
    mach/wait_queue.c
    mm/pmm.c
    mm/pmap.c
    mm/copy.c
    mm/zone.c
    mm/vm_object.c
    bsd/proc.c
    bsd/fileproc.c
    bsd/launchd.c
    bsd/syscall.c
    vfs/vfs.c
    vfs/devfs.c
    vfs/pty.c
    vfs/pipe.c
    bsd/mach_loader.c
    vfs/fat32.c
    drivers/ata.c
    drivers/net/e1000.c
    net/mbuf.c
    net/if.c
    net/if_loop.c
    net/ethernet.c
    net/arp.c
    net/ip.c
    net/icmp.c
    net/udp.c
    net/tcp.c
    net/dhcp.c
    net/uipc_socket.c
    net/net_init.c
    xiukit/xiukit_pci.cpp
    xiukit/runtime.cpp
    xiukit/hid.cpp
    xiukit/xhci.cpp
    xiukit/ahci.cpp
    arch/x86_64/gdt.c
    arch/x86_64/idt.c
    arch/x86_64/lapic.c
    arch/x86_64/smp.c
    arch/x86_64/interrupt.S
    arch/x86_64/syscall_entry.S
    arch/x86_64/scheduler.c
    arch/x86_64/switch.S
)

target_include_directories(xiu_kernel PRIVATE
    ${CMAKE_SOURCE_DIR}/kernel/include
    ${CMAKE_SOURCE_DIR}/kernel
    ${CMAKE_SOURCE_DIR}/drivers
)

target_link_options(xiu_kernel PRIVATE "-T" "${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/linker.ld")

# Enable C++ for XIU-Kit
set_target_properties(xiu_kernel PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(xiu_kernel PRIVATE 
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics -std=c++20>
)

option(XIU_VERBOSE "Enable verbose kernel and syscall debug logs" OFF)
if(XIU_VERBOSE)
    target_compile_definitions(xiu_kernel PRIVATE XIU_VERBOSE=1)
endif()

# All linker flags are handled by the toolchain bypass on macOS
# to ensure ELF compatibility.

# Ensure symbols are not stripped for debugging
set_target_properties(xiu_kernel PROPERTIES SUFFIX ".elf")
