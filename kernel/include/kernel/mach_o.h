/* =============================================================================
 * Chimera Operating System — Mach-O 64-bit Format Definitions (XNU Aligned)
 * kernel/include/kernel/mach_o.h
 * ============================================================================= */

#pragma once
#ifndef CHIMERA_MACH_O_H
#define CHIMERA_MACH_O_H

#include <kernel/chimera_types.h>

// forward declarations
struct chimera_task;

// mach-O Magic constants
#define MH_MAGIC_64         0xfeedfacfU
#define MH_CIGAM_64         0xcffaedfeU

// cpu Types
#define CPU_ARCH_ABI64      0x01000000
#define CPU_TYPE_X86        7
#define CPU_TYPE_X86_64     (CPU_ARCH_ABI64 | CPU_TYPE_X86)
#define CPU_TYPE_ARM        12
#define CPU_TYPE_ARM64      (CPU_ARCH_ABI64 | CPU_TYPE_ARM)

// file types
#define MH_OBJECT           0x1
#define MH_EXECUTE          0x2
#define MH_DYLIB            0x6
#define MH_DYLINKER         0x7

// mach-O 64-bit Header
typedef struct mach_header_64 {
    u32 magic;
    u32 cputype;
    u32 cpusubtype;
    u32 filetype;
    u32 ncmds;
    u32 sizeofcmds;
    u32 flags;
    u32 reserved;
} mach_header_64_t;

// load Command IDs
#define LC_REQ_DYLD         0x80000000U
#define LC_SEGMENT_64       0x19U
#define LC_UNIXTHREAD       0x5U
#define LC_LOAD_DYLIB       0xcU
#define LC_LOAD_DYLINKER    0xeU
#define LC_MAIN             (0x28U | LC_REQ_DYLD) /* 0x80000028 */

// vm Protection
#define VM_PROT_NONE        0x0
#define VM_PROT_READ        0x1
#define VM_PROT_WRITE       0x2
#define VM_PROT_EXECUTE     0x4

// generic Load Command
typedef struct load_command {
    u32 cmd;
    u32 cmdsize;
} load_command_t;

// 64-bit Segment Command
typedef struct segment_command_64 {
    u32 cmd;
    u32 cmdsize;
    char segname[16];
    u64 vmaddr;
    u64 vmsize;
    u64 fileoff;
    u64 filesize;
    u32 maxprot;
    u32 initprot;
    u32 nsects;
    u32 flags;
} segment_command_64_t;

// 64-bit Section Header
typedef struct section_64 {
    char sectname[16];
    char segname[16];
    u64 addr;
    u64 size;
    u32 offset;
    u32 align;
    u32 reloff;
    u32 nreloc;
    u32 flags;
    u32 reserved1;
    u32 reserved2;
    u32 reserved3;
} section_64_t;

// lc_main entry point command
typedef struct entry_point_command {
    u32 cmd;
    u32 cmdsize;
    u64 entryoff;
    u64 stacksize;
} entry_point_command_t;

// x86_64 Thread State in LC_UNIXTHREAD
typedef struct x86_thread_state64 {
    u64 rax;
    u64 rbx;
    u64 rcx;
    u64 rdx;
    u64 rdi;
    u64 rsi;
    u64 rbp;
    u64 rsp;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
    u64 rip;
    u64 rflags;
    u64 cs;
    u64 fs;
    u64 gs;
} x86_thread_state64_t;

typedef struct thread_command {
    u32 cmd;
    u32 cmdsize;
    u32 flavor;
    u32 count;
    x86_thread_state64_t state;
} thread_command_t;

// loader functions
#ifdef __cplusplus
extern "C" {
#endif

int mach_load(void *module_ptr, struct chimera_task *out_task, uintptr_t *entry_point, uintptr_t *user_stack);
int mach_load_args(void *module_ptr, struct chimera_task *out_task,
                   uintptr_t *entry_point, uintptr_t *user_stack,
                   const char *arg0, char *const argv[], char *const envp[]);

#ifdef __cplusplus
}
#endif

#endif /* CHIMERA_MACH_O_H */
