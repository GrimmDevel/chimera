# =============================================================================
# XIU Cross-Compilation Toolchain — x86_64 bare-metal ELF
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-x86_64.cmake ..
# =============================================================================

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  x86_64)

find_program(LLVM_CLANG    NAMES /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang clang REQUIRED)
find_program(LLVM_CLANGXX  NAMES /opt/homebrew/opt/llvm/bin/clang++ /usr/local/opt/llvm/bin/clang++ clang++ REQUIRED)
find_program(LLVM_LLD      NAMES /opt/homebrew/opt/llvm/bin/ld.lld /opt/homebrew/bin/ld.lld /usr/local/opt/llvm/bin/ld.lld ld.lld REQUIRED)
find_program(LLVM_AR       NAMES /opt/homebrew/opt/llvm/bin/llvm-ar /usr/local/opt/llvm/bin/llvm-ar llvm-ar ar REQUIRED)
find_program(LLVM_OBJCOPY  NAMES /opt/homebrew/opt/llvm/bin/llvm-objcopy /usr/local/opt/llvm/bin/llvm-objcopy llvm-objcopy objcopy)

set(CMAKE_C_COMPILER    ${LLVM_CLANG})
set(CMAKE_CXX_COMPILER  ${LLVM_CLANGXX})
set(CMAKE_ASM_COMPILER  ${LLVM_CLANG})
set(CMAKE_LINKER        ld)
set(CMAKE_AR            ${LLVM_AR})
set(CMAKE_OBJCOPY       ${LLVM_OBJCOPY})

set(CHIMERA_TARGET_TRIPLE "x86_64-apple-darwin")
set(CMAKE_C_COMPILER_TARGET   ${CHIMERA_TARGET_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${CHIMERA_TARGET_TRIPLE})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Bypass compiler checks which fail on macOS when cross-compiling to ELF
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

# Disable CMake's dependency tracking for the link step to prevent it from passing compiler driver flags like -Xlinker
set(CMAKE_LINK_DEPENDS_USE_COMPILER FALSE CACHE BOOL "" FORCE)
set(CMAKE_C_LINK_DEPENDS_USE_COMPILER FALSE CACHE BOOL "" FORCE)
set(CMAKE_CXX_LINK_DEPENDS_USE_COMPILER FALSE CACHE BOOL "" FORCE)
set(CMAKE_C_LINKER_DEPFILE_SUPPORTED FALSE CACHE BOOL "" FORCE)
set(CMAKE_CXX_LINKER_DEPFILE_SUPPORTED FALSE CACHE BOOL "" FORCE)
set(CMAKE_C_LINKER_WRAPPER_FLAG "" CACHE STRING "" FORCE)
set(CMAKE_CXX_LINKER_WRAPPER_FLAG "" CACHE STRING "" FORCE)
set(CMAKE_ASM_LINKER_WRAPPER_FLAG "" CACHE STRING "" FORCE)

add_compile_options(
    -target ${CHIMERA_TARGET_TRIPLE}
    -march=x86-64-v2
    -mcmodel=kernel
    -mno-red-zone
    -mno-sse -mno-sse2 -mno-avx -mno-mmx
    -mstack-alignment=16
    -DXIU_ARCH_x86_64=1
    -DXIU_PAGE_SIZE=4096
    -DXIU_KERNEL_BASE=0xFFFFFFFF80000000ULL
)

# All link options are now handled by the custom CMAKE_C_LINK_EXECUTABLE 
# to bypass Clang driver issues on macOS.

set(CHIMERA_ARCH        "x86_64"             CACHE STRING "" FORCE)
set(CHIMERA_PAGE_SIZE   "4096"               CACHE STRING "" FORCE)
set(CHIMERA_KERNEL_BASE "0xFFFFFFFF80000000" CACHE STRING "" FORCE)
set(CHIMERA_PHYS_BASE   "0x0000000001000000" CACHE STRING "" FORCE)
set(CHIMERA_STACK_SIZE  "0x8000"             CACHE STRING "" FORCE)

set(CMAKE_ASM_FLAGS "-target ${CHIMERA_TARGET_TRIPLE}")

# Use compiler driver to link on macOS for Mach-O
set(CMAKE_C_LINK_EXECUTABLE
    "${CMAKE_C_COMPILER} -target ${CHIMERA_TARGET_TRIPLE} <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS>  -o <TARGET> <LINK_LIBRARIES>")
set(CMAKE_CXX_LINK_EXECUTABLE
    "${CMAKE_CXX_COMPILER} -target ${CHIMERA_TARGET_TRIPLE} <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS>  -o <TARGET> <LINK_LIBRARIES>")
