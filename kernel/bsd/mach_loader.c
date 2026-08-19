/* =============================================================================
 * XIU Operating System — Mach-O 64-bit Binary Loader (XNU Aligned)
 * kernel/bsd/mach_loader.c
 * ============================================================================= */

#include <kernel/mach_o.h>
#include <kernel/proc.h>
#include <kernel/panic.h>
#include <kernel/xiu_types.h>

extern void *kalloc(usize size);
extern void kfree(void *ptr);
extern void kprintf(const char *fmt, ...);

extern xiu_paddr_t pmm_alloc_page(void);
extern void pmm_free_page(u64 addr);

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITE   (1ULL << 1)
#define PAGE_USER    (1ULL << 2)

static inline u64 *get_table_ptr(u64 phys) {
    return (u64 *)(phys + g_hhdm_base);
}

extern u64 pmap_map_user_page(u64 target_pml4_phys, u64 vaddr, u64 paddr, u32 flags);

static inline bool seg_name_is(const char *segname, const char *name) {
    for (int i = 0; i < 16; i++) {
        if (name[i] == '\0') return true;
        if (segname[i] != name[i]) return false;
    }
    return true;
}

void mach_load_args(void *module_ptr, struct xiu_task *out_task,
                    uintptr_t *entry_point, uintptr_t *user_stack,
                    const char *arg0, char *const argv[], char *const envp[]) {
    if (!module_ptr || !out_task || !out_task->ta_vm_map) {
        xiu_panic("mach_load_args: Invalid module_ptr or out_task!");
    }

    mach_header_64_t *hdr = (mach_header_64_t *)module_ptr;
    if (hdr->magic != MH_MAGIC_64) {
        kprintf("[mach_load_args] ERROR: Invalid Mach-O magic: 0x%x (expected 0x%x)\n",
                hdr->magic, MH_MAGIC_64);
        xiu_panic("mach_load_args: Invalid Mach-O magic");
    }

    if (hdr->cputype != CPU_TYPE_X86_64) {
        kprintf("[mach_load_args] ERROR: Unsupported cputype 0x%x\n", hdr->cputype);
        xiu_panic("mach_load_args: Unsupported architecture");
    }

    u64 text_vmaddr = 0;
    u64 parsed_entry = 0;
    u8 *cmd_ptr = (u8 *)module_ptr + sizeof(mach_header_64_t);

    for (u32 i = 0; i < hdr->ncmds; i++) {
        load_command_t *cmd = (load_command_t *)cmd_ptr;

        if (cmd->cmd == LC_SEGMENT_64) {
            segment_command_64_t *seg = (segment_command_64_t *)cmd_ptr;

            // check if this is the text segment
            if (seg_name_is(seg->segname, "__TEXT")) {
                text_vmaddr = seg->vmaddr;
            }

            // __pagezero is left unmapped for NULL-dereference protection
            if (!seg_name_is(seg->segname, "__PAGEZERO") && seg->vmsize > 0) {
                u64 vaddr = seg->vmaddr;
                u64 memsz = seg->vmsize;
                u64 filesz = seg->filesize;
                u64 file_off = seg->fileoff;

                u32 page_flags = PAGE_USER;
                if (seg->initprot & VM_PROT_WRITE) page_flags |= PAGE_WRITE;

                u64 start_vaddr = vaddr & ~0xFFFULL;
                u64 end_vaddr = (vaddr + memsz + 4095) & ~0xFFFULL;

                for (u64 page_vaddr = start_vaddr; page_vaddr < end_vaddr; page_vaddr += 4096) {
                    u64 phys = pmm_alloc_page();
                    if (!phys) {
                        xiu_panic("mach_load_args: pmm_alloc_page() returned NULL");
                    }

                    u64 actual_phys = pmap_map_user_page((u64)out_task->ta_vm_map, page_vaddr, phys, page_flags);
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
            entry_point_command_t *ep = (entry_point_command_t *)cmd_ptr;
            parsed_entry = text_vmaddr + ep->entryoff;
        } else if (cmd->cmd == LC_UNIXTHREAD) {
            thread_command_t *tc = (thread_command_t *)cmd_ptr;
            parsed_entry = tc->state.rip;
        }

        cmd_ptr += cmd->cmdsize;
    }

    if (parsed_entry == 0) {
        parsed_entry = text_vmaddr;
    }
    *entry_point = (uintptr_t)parsed_entry;

    // allocate user stack 16KB
    u64 stack_top = 0x7FFFFFFF0000ULL;
    u64 top_actual_phys = 0;
    for (int i = 0; i < 4; i++) {
        u64 vaddr = stack_top - 4096 * (i + 1);
        u64 phys = pmm_alloc_page();
        if (!phys) {
            xiu_panic("mach_load_args: pmm_alloc_page() returned NULL for stack");
        }

        u64 actual_phys = pmap_map_user_page((u64)out_task->ta_vm_map, vaddr, phys,
                                             PAGE_USER | PAGE_WRITE);
        void *stack_page = get_table_ptr(actual_phys);
        __builtin_memset(stack_page, 0, 4096);

        if (i == 0) {
            top_actual_phys = actual_phys;
        }
    }

    u8 *kstack = (u8 *)(HHDM_BASE + (top_actual_phys & ~0xFFFULL) + 4096);
    u64 sp = stack_top;

    const char *kargv[16];
    const char *kenvp[16];
    int argc = 0, envc = 0;

    if (argv) {
        while (argc < 15 && argv[argc]) {
            kargv[argc] = argv[argc];
            argc++;
        }
    }
    if (argc == 0) {
        kargv[argc++] = arg0 ? arg0 : "/bin/program";
    }
    kargv[argc] = nullptr;

    if (envp) {
        while (envc < 15 && envp[envc]) {
            kenvp[envc] = envp[envc];
            envc++;
        }
    }
    kenvp[envc] = nullptr;

    u64 argv_user[16];
    u64 envp_user[16];

    // copy envp strings
    for (int i = envc - 1; i >= 0; i--) {
        usize len = __builtin_strlen(kenvp[i]) + 1;
        sp -= len;
        __builtin_memcpy(kstack - (stack_top - sp), kenvp[i], len);
        envp_user[i] = sp;
    }

    // copy argv strings
    for (int i = argc - 1; i >= 0; i--) {
        usize len = __builtin_strlen(kargv[i]) + 1;
        sp -= len;
        __builtin_memcpy(kstack - (stack_top - sp), kargv[i], len);
        argv_user[i] = sp;
    }

    // 16-byte align
    sp &= ~0xFULL;

    // darwin stack layout
    sp -= 8;
    *(u64 *)(kstack - (stack_top - sp)) = 0;

    sp -= 8;
    *(u64 *)(kstack - (stack_top - sp)) = 0;
    for (int i = envc - 1; i >= 0; i--) {
        sp -= 8;
        *(u64 *)(kstack - (stack_top - sp)) = envp_user[i];
    }

    sp -= 8;
    *(u64 *)(kstack - (stack_top - sp)) = 0;
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 8;
        *(u64 *)(kstack - (stack_top - sp)) = argv_user[i];
    }

    sp -= 8;
    *(u64 *)(kstack - (stack_top - sp)) = (u64)argc;

    *user_stack = (uintptr_t)sp;
}

void mach_load(void *module_ptr, struct xiu_task *out_task, uintptr_t *entry_point, uintptr_t *user_stack) {
    const char *argv[] = { "/bin/sh", nullptr };
    const char *envp[] = { nullptr };
    mach_load_args(module_ptr, out_task, entry_point, user_stack,
                   "/bin/sh", (char *const *)argv, (char *const *)envp);
}
