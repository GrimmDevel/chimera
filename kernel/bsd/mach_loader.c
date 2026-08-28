/* =============================================================================
 * Chimera Operating System — Mach-O 64-bit Binary Loader (XNU Aligned)
 * kernel/bsd/mach_loader.c
 * ============================================================================= */

#include <kernel/mach_o.h>
#include <kernel/proc.h>
#include <kernel/panic.h>
#include <kernel/chimera_types.h>

#define ENOEXEC 8
#define ENOMEM  12
#define EINVAL  22
#define E2BIG   7

#define MAX_ARG_COUNT 128
#define MAX_ENV_COUNT 128
#define MAX_ARG_DATA_SIZE 262144 /* 256 KB (ARG_MAX) */

extern void *kalloc(usize size);
extern void kfree(void *ptr);
extern void kprintf(const char *fmt, ...);

extern chimera_paddr_t pmm_alloc_page(void);
extern void pmm_release_page(chimera_paddr_t addr);

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITE   (1ULL << 1)
#define PAGE_USER    (1ULL << 2)

extern u64 g_hhdm_base;

static inline u64 *get_table_ptr(u64 phys) {
    return (u64 *)(phys + g_hhdm_base);
}

extern u64 pmap_map_user_page(u64 target_pml4_phys, u64 vaddr, u64 paddr, u32 flags);
extern u64 pmap_extract(u64 pml4_phys, u64 vaddr);

static inline bool seg_name_is(const char *segname, const char *name) {
    for (int i = 0; i < 16; i++) {
        if (name[i] == '\0') return true;
        if (segname[i] != name[i]) return false;
    }
    return true;
}

static int write_user_stack(u64 pml4_phys, u64 vaddr, const void *src, usize len) {
    const u8 *s = (const u8 *)src;
    while (len > 0) {
        u64 phys = pmap_extract(pml4_phys, vaddr);
        if (!phys) return -1;
        usize page_off = vaddr & 0xFFF;
        usize chunk = 4096 - page_off;
        if (chunk > len) chunk = len;
        void *dst = (void *)((phys & ~0xFFFULL) + g_hhdm_base + page_off);
        __builtin_memcpy(dst, s, chunk);
        vaddr += chunk;
        s += chunk;
        len -= chunk;
    }
    return 0;
}

static inline int write_user_stack_u64(u64 pml4_phys, u64 vaddr, u64 val) {
    return write_user_stack(pml4_phys, vaddr, &val, sizeof(u64));
}

int mach_load_args(void *module_ptr, struct chimera_task *out_task,
                   uintptr_t *entry_point, uintptr_t *user_stack,
                   const char *arg0, char *const argv[], char *const envp[]) {
    if (!module_ptr || !out_task || !out_task->ta_vm_map || !entry_point || !user_stack) {
        return -EINVAL;
    }

    mach_header_64_t *hdr = (mach_header_64_t *)module_ptr;
    if (hdr->magic != MH_MAGIC_64) {
        kprintf("[mach_load_args] ERROR: Invalid Mach-O magic: 0x%x\n", hdr->magic);
        return -ENOEXEC;
    }

    if (hdr->cputype != CPU_TYPE_X86_64) {
        kprintf("[mach_load_args] ERROR: Unsupported cputype 0x%x\n", hdr->cputype);
        return -ENOEXEC;
    }

    if (hdr->ncmds > 256 || hdr->sizeofcmds > 65536) {
        kprintf("[mach_load_args] ERROR: Invalid command table size (ncmds=%u, size=%u)\n",
                hdr->ncmds, hdr->sizeofcmds);
        return -ENOEXEC;
    }

    u64 text_vmaddr = 0;
    u64 parsed_entry = 0;
    u8 *cmd_ptr = (u8 *)module_ptr + sizeof(mach_header_64_t);
    u8 *cmd_end = cmd_ptr + hdr->sizeofcmds;

    for (u32 i = 0; i < hdr->ncmds; i++) {
        if (cmd_ptr + sizeof(load_command_t) > cmd_end) {
            return -ENOEXEC;
        }

        load_command_t *cmd = (load_command_t *)cmd_ptr;
        if (cmd->cmdsize < sizeof(load_command_t) || cmd_ptr + cmd->cmdsize > cmd_end) {
            return -ENOEXEC;
        }

        if (cmd->cmd == LC_SEGMENT_64) {
            if (cmd->cmdsize < sizeof(segment_command_64_t)) {
                return -ENOEXEC;
            }
            segment_command_64_t *seg = (segment_command_64_t *)cmd_ptr;

            if (seg_name_is(seg->segname, "__TEXT")) {
                text_vmaddr = seg->vmaddr;
            }

            // __PAGEZERO is left unmapped for NULL-dereference protection
            if (!seg_name_is(seg->segname, "__PAGEZERO") && seg->vmsize > 0) {
                u64 vaddr = seg->vmaddr;
                u64 memsz = seg->vmsize;
                u64 filesz = seg->filesize;
                u64 file_off = seg->fileoff;

                // Validate segment is strictly within user space
                if (vaddr < 0x1000 || vaddr >= 0x0000800000000000ULL ||
                    memsz >= 0x0000800000000000ULL || (vaddr + memsz) > 0x0000800000000000ULL) {
                    kprintf("[mach_load_args] ERROR: Segment '%s' out of user bounds (vmaddr=0x%llx, size=0x%llx)\n",
                            seg->segname, vaddr, memsz);
                    return -ENOEXEC;
                }

                u32 page_flags = PAGE_USER;
                if (seg->initprot & VM_PROT_WRITE) page_flags |= PAGE_WRITE;

                u64 start_vaddr = vaddr & ~0xFFFULL;
                u64 end_vaddr = (vaddr + memsz + 4095) & ~0xFFFULL;

                for (u64 page_vaddr = start_vaddr; page_vaddr < end_vaddr; page_vaddr += 4096) {
                    u64 phys = pmm_alloc_page();
                    if (!phys || phys == (chimera_paddr_t)-1) {
                        return -ENOMEM;
                    }

                    u64 actual_phys = pmap_map_user_page((u64)out_task->ta_vm_map, page_vaddr, phys, page_flags);
                    if (!actual_phys) {
                        pmm_release_page(phys);
                        return -ENOMEM;
                    }
                    void *page_hhdm = get_table_ptr(actual_phys);
                    __builtin_memset(page_hhdm, 0, 4096);

                    // copy initialized file data
                    u64 copy_start = (vaddr > page_vaddr) ? vaddr : page_vaddr;
                    u64 copy_end = (vaddr + filesz < page_vaddr + 4096) ? (vaddr + filesz) : (page_vaddr + 4096);

                    if (copy_start < copy_end) {
                        u64 dst_offset = copy_start - page_vaddr;
                        u64 src_offset = file_off + (copy_start - vaddr);
                        usize copy_len = copy_end - copy_start;
                        __builtin_memcpy((u8 *)page_hhdm + dst_offset, (u8 *)module_ptr + src_offset, copy_len);
                    }
                }
            }
        } else if (cmd->cmd == LC_MAIN) {
            if (cmd->cmdsize >= sizeof(entry_point_command_t)) {
                entry_point_command_t *ep = (entry_point_command_t *)cmd_ptr;
                parsed_entry = text_vmaddr + ep->entryoff;
            }
        } else if (cmd->cmd == LC_UNIXTHREAD) {
            if (cmd->cmdsize >= sizeof(thread_command_t)) {
                thread_command_t *tc = (thread_command_t *)cmd_ptr;
                parsed_entry = tc->state.rip;
            }
        }

        cmd_ptr += cmd->cmdsize;
    }

    if (parsed_entry == 0) {
        parsed_entry = text_vmaddr;
    }
    if (parsed_entry < 0x1000 || parsed_entry >= 0x0000800000000000ULL) {
        return -ENOEXEC;
    }
    *entry_point = (uintptr_t)parsed_entry;

    // allocate user stack 8MB (2048 pages)
    u64 stack_top = 0x7FFFFFFF0000ULL;
    for (int i = 0; i < 2048; i++) {
        u64 vaddr = stack_top - 4096 * (i + 1);
        u64 phys = pmm_alloc_page();
        if (!phys || phys == (chimera_paddr_t)-1) {
            return -ENOMEM;
        }

        u64 actual_phys = pmap_map_user_page((u64)out_task->ta_vm_map, vaddr, phys,
                                             PAGE_USER | PAGE_WRITE);
        if (!actual_phys) {
            pmm_release_page(phys);
            return -ENOMEM;
        }
        void *stack_page = get_table_ptr(actual_phys);
        __builtin_memset(stack_page, 0, 4096);
    }

    u64 pml4_phys = (u64)out_task->ta_vm_map;
    u64 sp = stack_top;

    const char *kargv[MAX_ARG_COUNT];
    const char *kenvp[MAX_ENV_COUNT];
    int argc = 0, envc = 0;
    usize total_arg_bytes = 0;

    if (argv) {
        while (argc < (MAX_ARG_COUNT - 1) && argv[argc]) {
            kargv[argc] = argv[argc];
            total_arg_bytes += __builtin_strlen(argv[argc]) + 1;
            argc++;
        }
    }
    if (argc == 0) {
        kargv[argc++] = arg0 ? arg0 : "/bin/program";
        total_arg_bytes += __builtin_strlen(kargv[0]) + 1;
    }
    kargv[argc] = nullptr;

    if (envp) {
        while (envc < (MAX_ENV_COUNT - 1) && envp[envc]) {
            kenvp[envc] = envp[envc];
            total_arg_bytes += __builtin_strlen(envp[envc]) + 1;
            envc++;
        }
    }
    kenvp[envc] = nullptr;

    if (total_arg_bytes > MAX_ARG_DATA_SIZE) {
        return -E2BIG;
    }

    u64 argv_user[MAX_ARG_COUNT];
    u64 envp_user[MAX_ENV_COUNT];

    // copy envp strings safely to stack
    for (int i = envc - 1; i >= 0; i--) {
        usize len = __builtin_strlen(kenvp[i]) + 1;
        sp -= len;
        if (write_user_stack(pml4_phys, sp, kenvp[i], len) != 0) {
            return -ENOMEM;
        }
        envp_user[i] = sp;
    }

    // copy argv strings safely to stack
    for (int i = argc - 1; i >= 0; i--) {
        usize len = __builtin_strlen(kargv[i]) + 1;
        sp -= len;
        if (write_user_stack(pml4_phys, sp, kargv[i], len) != 0) {
            return -ENOMEM;
        }
        argv_user[i] = sp;
    }

    // 16-byte align
    sp &= ~0xFULL;

    // Darwin stack layout
    sp -= 8;
    write_user_stack_u64(pml4_phys, sp, 0); // null terminator

    sp -= 8;
    write_user_stack_u64(pml4_phys, sp, 0);
    for (int i = envc - 1; i >= 0; i--) {
        sp -= 8;
        write_user_stack_u64(pml4_phys, sp, envp_user[i]);
    }

    sp -= 8;
    write_user_stack_u64(pml4_phys, sp, 0);
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 8;
        write_user_stack_u64(pml4_phys, sp, argv_user[i]);
    }

    sp -= 8;
    write_user_stack_u64(pml4_phys, sp, (u64)argc);

    *user_stack = (uintptr_t)sp;
    return 0;
}

int mach_load(void *module_ptr, struct chimera_task *out_task, uintptr_t *entry_point, uintptr_t *user_stack) {
    const char *argv[] = { "/bin/sh", nullptr };
    const char *envp[] = { nullptr };
    return mach_load_args(module_ptr, out_task, entry_point, user_stack,
                          "/bin/sh", (char *const *)argv, (char *const *)envp);
}
