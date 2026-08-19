// kernel entry point
#include <kernel/xiu_types.h>
#include <limine/limine.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/ipc_port.h>
#include <kernel/ipc_message.h>
#include <kernel/vfs_node.h>
#include <kernel/vm_object.h>
#include <kernel/proc.h>
#include <xiukit/xiukit_pci.hpp>

// subsystem init prototypes

// phase 0: Console
extern void console_init(void);
extern void serial_puts(const char *s);

// simple Bump Allocator for Kernel
static u8 s_kernel_heap[1024 * 1024]; // 1mb
static usize s_heap_ptr = 0;

void *kernel_alloc(usize size) {
    if (s_heap_ptr + size > sizeof(s_kernel_heap)) return nullptr;
    void *ptr = &s_kernel_heap[s_heap_ptr];
    s_heap_ptr += (size + 15) & ~15; // 16-byte align
    return ptr;
}

extern void kprintf(const char *fmt, ...);

// phase 1: Physical Memory Manager
extern void pmm_init(xiu_paddr_t memmap_base, usize memmap_count);
extern usize pmm_total_pages(void);
extern usize pmm_free_pages(void);

// phase 2: Virtual Memory
extern void pmap_bootstrap(void);
extern void vm_map_init(void);
extern void vm_object_init(void);

extern void zone_init(void);
extern void *kalloc(usize size);
extern void  kfree(void *ptr);
extern void scheduler_add_thread(xiu_thread_t *th);

// phase 4: Mach IPC
extern void ipc_init(void);
extern ipc_port_t *ipc_port_kernel_bootstrap;

// phase 5: BSD layer
extern void proc_init(void);

// phase 6: VFS
extern xiu_error_t vfs_init(void);
extern xiu_error_t vfs_build_root_hierarchy(void);

// phase 7: XIU-Kit driver framework
extern void xiu_kit_init(void);
extern void xiu_kit_start_matching(void);
extern void xiukit_hid_init(void);

// phase 8: launchd-xiu
extern xiu_error_t launchd_xiu_start(void);

// phase 9: Scheduler
extern void scheduler_init(void);
extern XIU_NORETURN void scheduler_run(void);

// stage 4: Ring 3 Transition
extern void gdt_init(void);
extern void mach_load(void *module_ptr, struct xiu_task *out_task, uptr *entry_point, uptr *user_stack);
extern XIU_NORETURN void task_switch_to_user(uptr entry_point, uptr user_stack);

// boot info passed from the boot stub

typedef struct XIU_PACKED xiu_boot_info {
    u64         magic;
    u32         version;
    u32         flags;

    xiu_paddr_t memmap_base;
    usize       memmap_count;
    usize       memmap_desc_size;

    xiu_paddr_t kernel_phys_start;
    xiu_paddr_t kernel_phys_end;
    xiu_vaddr_t kernel_virt_start;

    xiu_paddr_t fb_base;
    u32         fb_width;
    u32         fb_height;
    u32         fb_stride;
    u32         fb_format;

    xiu_paddr_t rsdp_base;
    xiu_paddr_t smbios_base;

    char        cmdline[256];
} xiu_boot_info_t;

#define XIU_BOOT_MAGIC  UINT64_C(0x584955424F4F5421)

// limine requests
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0
};

static volatile struct limine_kernel_address_request kernel_addr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0
};

// global boot info
xiu_boot_info_t g_boot_info_storage;
xiu_boot_info_t *g_boot_info = &g_boot_info_storage;

// global physical address for devfs mmap
u64 g_fb_phys_addr = 0;
// higher Half Direct Map base — used by sys_mmap to zero anonymous pages
u64 g_hhdm_base = 0;
// global CPU count from SMP
u32 g_cpu_count = 1; // default to 1 if SMP not available

// early banner
static void print_banner(void) {
    serial_puts("\n");
    serial_puts("  ╔═══════════════════════════════════════╗\n");
    serial_puts("  ║   XIU Operating System  v0.1.0        ║\n");
    serial_puts("  ║   Stage 1 — Foundation Kernel         ║\n");
    serial_puts("  ║   Hybrid Mach/BSD Kernel               ║\n");
    serial_puts("  ╚═══════════════════════════════════════╝\n");
    serial_puts("\n");
}

// boot info validation
static void validate_boot_info(const xiu_boot_info_t *bi) {
    if (bi->magic != XIU_BOOT_MAGIC) {
        xiu_panic("xiu_kernel_main: invalid boot info magic "
                  "(got 0x%016llx, expected 0x%016llx)\n",
                  (unsigned long long)bi->magic,
                  (unsigned long long)XIU_BOOT_MAGIC);
    }
    if (bi->memmap_count == 0 || bi->memmap_base == 0) {
        kprintf("[XIU] Warning: No memory map from bootloader.\n");
    }
    // relaxing image bounds check for early Stage 1 Limine boot
}

// helper: log subsystem init step
static void init_log(const char *subsystem, bool ok) {
    if (ok) {
        kprintf("  [  OK  ]  %s\n", subsystem);
    } else {
        kprintf("  [ FAIL ]  %s\n", subsystem);
        xiu_panic("Kernel initialization failed in subsystem: %s\n", subsystem);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * xiu_kernel_main — The Primary Kernel Entry Point
 *
 * Called by the Limine bootloader with interrupts DISABLED.
 * ═══════════════════════════════════════════════════════════════════════════ */
void xiu_kernel_main(void) {
    // higher Half Direct Map base — MUST be initialized first!
    if (hhdm_request.response) {
        g_hhdm_base = hhdm_request.response->offset;
    } else {
        g_hhdm_base = 0xffff800000000000ULL;
    }

    // early console & framebuffer
    console_init();

    if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0) {
        struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
        
        g_boot_info->fb_base = (xiu_paddr_t)((u64)fb->address - g_hhdm_base);
        g_boot_info->fb_width = fb->width;
        g_boot_info->fb_height = fb->height;
        g_boot_info->fb_stride = fb->pitch / (fb->bpp / 8);
        g_fb_phys_addr = g_boot_info->fb_base;

        extern void console_fb_init(u32 *fb_ptr, u32 width, u32 height, u32 pitch);
        console_fb_init((u32 *)fb->address, fb->width, fb->height, fb->pitch / (fb->bpp / 8));
    }

    serial_puts("[XIU] Early boot — console active\n");
    print_banner();

    // translate Limine info into xiu_boot_info_t
    g_boot_info->magic = XIU_BOOT_MAGIC;
    
    if (memmap_request.response) {
        g_boot_info->memmap_base = (xiu_paddr_t)memmap_request.response->entries;
        g_boot_info->memmap_count = memmap_request.response->entry_count;
    }

    if (rsdp_request.response) {
        g_boot_info->rsdp_base = (xiu_paddr_t)rsdp_request.response->address;
    }

    if (smp_request.response) {
        g_cpu_count = (u32)smp_request.response->cpu_count;
        kprintf("[XIU] SMP: Detected %u CPU(s)\n", g_cpu_count);
    }

    if (kernel_addr_request.response) {
        g_boot_info->kernel_phys_start = kernel_addr_request.response->physical_base;
        g_boot_info->kernel_virt_start = kernel_addr_request.response->virtual_base;
        g_boot_info->kernel_phys_end = g_boot_info->kernel_phys_start + 0x100000;
    }

    validate_boot_info(g_boot_info);

    kprintf("[XIU] Kernel loaded via Limine Protocol\n");

    kprintf("[XIU] Framebuffer: %ux%u @ phys=0x%llx\n",
            g_boot_info->fb_width, g_boot_info->fb_height,
            (unsigned long long)g_boot_info->fb_base);

    kprintf("\n[XIU] === Kernel Initialization ===\n\n");

    kprintf("[XIU] Initializing x86_64 Core (GDT, TSS, IDT)...\n");
    gdt_init();
    
    extern void idt_init(void);
    idt_init();
    
    kprintf("  [  OK  ]  Architecture GDT\n");

    // phase 1: Physical Memory Manager
    kprintf("[XIU] Phase 1: Physical Memory Manager\n");
    pmm_init(g_boot_info->memmap_base, g_boot_info->memmap_count);
    kprintf("        Total RAM  : %zu MiB\n",
            (pmm_total_pages() * XIU_PAGE_SIZE) / (1024 * 1024));
    kprintf("        Free pages : %zu (%zu MiB)\n",
            pmm_free_pages(),
            (pmm_free_pages() * XIU_PAGE_SIZE) / (1024 * 1024));
    init_log("Physical Memory Manager", true);

    // phase 2: Virtual Memory
    kprintf("[XIU] Phase 2: Virtual Memory Manager\n");
    pmap_bootstrap();
    init_log("Architecture PMAP", true);

    vm_object_init();
    init_log("VM Object subsystem", true);

    vm_map_init();
    init_log("VM Map subsystem", true);

    kprintf("[XIU] Phase 3: Zone Allocator\n");
    zone_init();
    init_log("Kernel zone allocator", true);

    kprintf("[XIU] Initializing Symmetric Multiprocessing (SMP)...\n");
    extern void smp_init(void);
    smp_init();

    kprintf("[XIU] Phase 4: Mach IPC subsystem\n");
    
    proc_init();
    XIU_ASSERT(task_kernel != NULL);
    XIU_ASSERT(proc_kernel != NULL);
    init_log("Mach Task/Proc foundation", true);

    ipc_init();
    XIU_ASSERT(ipc_port_kernel_bootstrap != NULL);
    init_log("Mach IPC (ports, spaces, kmsg)", true);

    kprintf("[XIU] Phase 5: BSD Personality Layer\n");
    kprintf("        kernel task  : id=0x%llx\n",
            (unsigned long long)task_kernel->ta_id);
    kprintf("        kernel proc  : pid=%u\n", proc_kernel->p_pid);
    init_log("BSD personality", true);

    // phase 6: Virtual File System
    kprintf("[XIU] Phase 6: Virtual File System\n");
    xiu_error_t err = vfs_init();
    init_log("VFS core", XIU_SUCCEEDED(err));

    err = vfs_build_root_hierarchy();
    init_log("Root directory hierarchy (/System /Library /Applications …)", XIU_SUCCEEDED(err));

    kprintf("        Root vnode   : %s  type=VDIR\n",
            vfs_root_vnode ? vfs_root_vnode->v_name : "(null)");

    // phase 7: XIU-Kit Driver Framework
    kprintf("[XIU] Phase 7: XIU-Kit Driver Framework\n");
    
    // enumerate PCI Bus
    xiukit_pci_init();

    extern void xiukit_hid_init(void);
    xiukit_hid_init();

    xiu_kit_init();
    xiu_kit_start_matching();
    init_log("XIU-Kit driver registry", true);
    init_log("Driver matching pass", true);
    init_log("Driver matching pass", true);

    kprintf("[XIU] Phase 7.5: Persistent Storage & FAT32 Filesystem\n");
    extern xiu_error_t ata_init(void);
    extern xiu_error_t fat32_init(void);
    ata_init();
    fat32_init();

    // phase 7.8: xiu-net darwin bsd network subsystem
    extern void net_init(void);
    net_init();

    kprintf("[XIU] Phase 8: launchd-xiu\n");
    err = launchd_xiu_start();
    init_log("launchd-xiu (PID 1)", XIU_SUCCEEDED(err));

    if (XIU_SUCCEEDED(err)) {
        kprintf("        launchd PID  : %u\n",
                proc_launchd ? proc_launchd->p_pid : 0u);
    }

    // phase 9: scheduler
    kprintf("\n[XIU] Phase 9: Starting scheduler\n");
    scheduler_init();

    if (module_request.response && module_request.response->module_count > 0) {
        kprintf("[XIU] Found %llu in-memory module(s).\n", 
                (unsigned long long)module_request.response->module_count);
        for (u64 i = 0; i < module_request.response->module_count; i++) {
            struct limine_file *module = module_request.response->modules[i];
            extern xiu_error_t vfs_register_module(const char *path, void *addr, usize size);
            vfs_register_module(module->path, module->address, module->size);
        }
    }

    extern void xiukit_xhci_dump_status(void);
    xiukit_xhci_dump_status();

    vnode_t *sh_vp = nullptr;
    extern xiu_error_t vfs_lookup(const char *path, vnode_t **vp_out);
    if (vfs_lookup("/bin/sh", &sh_vp) == XIU_SUCCESS && sh_vp) {
        kprintf("[XIU] Auto-starting interactive console shell (/bin/sh)...\n");
        
        void *elf_addr = sh_vp->v_data;
        xiu_paddr_t temp_phys = (xiu_paddr_t)-1;
        usize temp_pages = 0;

        if (sh_vp->v_op && __builtin_strcmp(sh_vp->v_op->vop_name, "fat32_file") == 0) {
            typedef struct {
                u32 start_cluster;
                u32 file_size;
                bool is_dir;
                char path[256];
            } fat32_node_info_t;

            fat32_node_info_t *nd = (fat32_node_info_t *)sh_vp->v_data;
            if (nd && nd->file_size > 0) {
                extern xiu_paddr_t pmm_alloc_pages(usize count);
                extern void pmm_free_contiguous(xiu_paddr_t base, usize count);
                temp_pages = (nd->file_size + 4095) / 4096;
                temp_phys = pmm_alloc_pages(temp_pages);
                if (temp_phys != (xiu_paddr_t)-1) {
                    elf_addr = (void *)(temp_phys + g_hhdm_base);
                    u32 actual = 0;
                    extern xiu_error_t fat32_read_file(u32 start_cluster, u32 file_size, u32 offset, void *dst, u32 len, u32 *bytes_read);
                    fat32_read_file(nd->start_cluster, nd->file_size, 0, elf_addr, nd->file_size, &actual);
                }
            }
        }

        if (elf_addr) {
            uptr entry_point = 0;
            uptr user_stack = 0;

            xiu_proc_t *proc = nullptr;
            proc_create(proc_launchd, "sh", &proc);
            xiu_task_t *task = proc->p_task;

            static xiu_thread_t s_sh_thread;
            __builtin_memset(&s_sh_thread, 0, sizeof(xiu_thread_t));
            s_sh_thread.th_task = task;
            task->ta_threads = &s_sh_thread;
            s_sh_thread.th_state = THREAD_STATE_READY;
            s_sh_thread.th_priority = 0;

            mach_load(elf_addr, task, &entry_point, &user_stack);
            s_sh_thread.th_context = (void *)entry_point;

            scheduler_add_thread(&s_sh_thread);

            extern void thread_init_stack(xiu_thread_t *th, void *entry, void *stack);
            thread_init_stack(&s_sh_thread, (void *)entry_point, (void *)user_stack);

            extern void scheduler_set_initial(xiu_thread_t *th);
            scheduler_set_initial(&s_sh_thread);
        }

        if (temp_pages > 0) {
            extern void pmm_free_contiguous(xiu_paddr_t base, usize count);
            pmm_free_contiguous(temp_phys, temp_pages);
        }
    } else {
        kprintf("[XIU] WARNING: /bin/sh not found in VFS or on disk!\n");
    }

    /*
     * scheduler_run() enables interrupts and drops into the idle thread.
     * It must never return.
     */
    scheduler_run();

    // unreachable — satisfy noreturn path for non-[[noreturn]] callers
    XIU_UNREACHABLE();
}

/* ── Kernel idle entry (lowest-priority thread) ─────────────────────────
 * The scheduler calls this function in the idle thread context when no
 * other thread is runnable.  On x86_64 we issue HLT to save power.
 */
XIU_USED XIU_COLD
static void xiu_idle_loop(void) {
    for (;;) {
#if defined(XIU_ARCH_x86_64)
        __asm__ volatile("sti; hlt" ::: "memory");
#elif defined(XIU_ARCH_arm64)
        __asm__ volatile("wfi" ::: "memory");
#else
        cpu_relax();
#endif
    }
}
