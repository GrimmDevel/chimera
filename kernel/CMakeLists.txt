# XIU Kernel — Core Subsystem Build
add_executable(chimera_kernel
    chimera_kernel_main.c
    console.c
    video_console.c
    iso_font.c
    lib.c
    kernel_stubs.c
    mach/task.c
    mach/ipc_port.c
    mach/ipc_kmsg.c
    mach/ipc_kobject.c
    mach/wait_queue.c
    mm/pmm.c
    mm/pmap.c
    mm/copy.c
    mm/zone.c
    mm/vm_object.c
    mm/vm_map.c
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
    chimerakit/chimerakit_pci.cpp
    chimerakit/runtime.cpp
    chimerakit/hid.cpp
    chimerakit/xhci.cpp
    chimerakit/ahci.cpp
    arch/x86_64/gdt.c
    arch/x86_64/idt.c
    arch/x86_64/lapic.c
    arch/x86_64/smp.c
    arch/x86_64/interrupt.S
    arch/x86_64/syscall_entry.S
    arch/x86_64/scheduler.c
    arch/x86_64/switch.S
)

target_include_directories(chimera_kernel PRIVATE
    ${CMAKE_SOURCE_DIR}/kernel/include
    ${CMAKE_SOURCE_DIR}/kernel
    ${CMAKE_SOURCE_DIR}/drivers
)

target_link_options(chimera_kernel PRIVATE 
    "-nostdlib"
    "-Wl,-e,_chimera_kernel_main"
    "-Wl,-pagezero_size,0"
    "-Wl,-image_base,0xffffff8000000000"
    "-Wl,-segalign,0x1000"
    "-Wl,-no_pie"
    "-static"
)

# Enable C++ for ChimeraKit
set_target_properties(chimera_kernel PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(chimera_kernel PRIVATE
    -nostdlib -nostdlibinc -fno-builtin -fno-stack-protector -O2 -Wall -Wextra
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-threadsafe-statics -std=c++20>
)

option(CHIMERA_VERBOSE "Enable verbose kernel and syscall debug logs" OFF)
if(CHIMERA_VERBOSE)
    target_compile_definitions(chimera_kernel PRIVATE CHIMERA_VERBOSE=1)
endif()

# All linker flags are handled by the toolchain bypass on macOS
# to ensure ELF compatibility.

# Ensure symbols are not stripped for debugging
set_target_properties(chimera_kernel PROPERTIES SUFFIX ".elf")
