# =============================================================================
# XIU Cross-Compilation Toolchain — ARM64/AArch64 bare-metal ELF
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake ..
# =============================================================================

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  aarch64)

find_program(LLVM_CLANG    NAMES /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang clang REQUIRED)
find_program(LLVM_CLANGXX  NAMES /opt/homebrew/opt/llvm/bin/clang++ /usr/local/opt/llvm/bin/clang++ clang++ REQUIRED)
find_program(LLVM_LLD      NAMES /opt/homebrew/opt/llvm/bin/ld.lld /opt/homebrew/bin/ld.lld /usr/local/opt/llvm/bin/ld.lld ld.lld REQUIRED)
find_program(LLVM_AR       NAMES /opt/homebrew/opt/llvm/bin/llvm-ar /usr/local/opt/llvm/bin/llvm-ar llvm-ar ar REQUIRED)
find_program(LLVM_OBJCOPY  NAMES /opt/homebrew/opt/llvm/bin/llvm-objcopy /usr/local/opt/llvm/bin/llvm-objcopy llvm-objcopy objcopy)

set(CMAKE_C_COMPILER    ${LLVM_CLANG})
set(CMAKE_CXX_COMPILER  ${LLVM_CLANGXX})
set(CMAKE_ASM_COMPILER  ${LLVM_CLANG})
set(CMAKE_LINKER        ${LLVM_LLD})
set(CMAKE_AR            ${LLVM_AR})
set(CMAKE_OBJCOPY       ${LLVM_OBJCOPY})

set(XIU_TARGET_TRIPLE "aarch64-unknown-none-elf")
set(CMAKE_C_COMPILER_TARGET   ${XIU_TARGET_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${XIU_TARGET_TRIPLE})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Bypass compiler checks
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
    -target ${XIU_TARGET_TRIPLE}
    -march=armv8.2-a+crypto+fp16
    -mcpu=cortex-a72
    -mstrict-align
    -DXIU_ARCH_arm64=1
    -DXIU_PAGE_SIZE=16384
    -DXIU_KERNEL_BASE=0xFFFFFF8000000000ULL
)

# All link options handled by custom CMAKE_C_LINK_EXECUTABLE

set(XIU_ARCH        "arm64"              CACHE STRING "" FORCE)
set(XIU_PAGE_SIZE   "16384"              CACHE STRING "" FORCE)
set(XIU_KERNEL_BASE "0xFFFFFF8000000000" CACHE STRING "" FORCE)
set(XIU_PHYS_BASE   "0x0000000040000000" CACHE STRING "" FORCE)
set(XIU_STACK_SIZE  "0x8000"             CACHE STRING "" FORCE)

set(CMAKE_ASM_FLAGS "-target ${XIU_TARGET_TRIPLE}")

# ── Linker Bypass for macOS (force ELF LLD) ────────────────────────────────
set(CMAKE_C_LINK_EXECUTABLE 
    "${LLVM_LLD} -flavor gnu <OBJECTS> -o <TARGET> <LINK_LIBRARIES> <LINK_FLAGS> -static -z max-page-size=0x4000 -z noexecstack")
set(CMAKE_CXX_LINK_EXECUTABLE 
    "${LLVM_LLD} -flavor gnu <OBJECTS> -o <TARGET> <LINK_LIBRARIES> <LINK_FLAGS> -static -z max-page-size=0x4000 -z noexecstack")

