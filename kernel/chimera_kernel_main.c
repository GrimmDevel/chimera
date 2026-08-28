// kernel entry point
#include <kernel/ipc_message.h>
#include <kernel/ipc_port.h>
#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/spinlock.h>
#include <kernel/vfs_node.h>
#include <kernel/vm_object.h>
#include <kernel/chimera_types.h>
#include <limine/limine.h>
#include <chimerakit/chimerakit_pci.hpp>

// subsystem init prototypes

// phase 0: Console
extern void console_init(void);
extern void serial_puts(const char *s);
extern void kprintf(const char *fmt, ...);

// phase 1: Physical Memory Manager
extern void pmm_init(chimera_paddr_t memmap_base, usize memmap_count);
extern usize pmm_total_pages(void);
extern usize pmm_free_pages(void);
extern void video_console_init_backbuffer(void);

// phase 2: Virtual Memory
extern void pmap_bootstrap(void);
extern void vm_map_init(void);
extern void vm_object_init(void);

extern void zone_init(void);
extern void *kalloc(usize size);
extern void kfree(void *ptr);
extern void scheduler_add_thread(chimera_thread_t *th);

// phase 4: Mach IPC
extern void ipc_init(void);
extern struct ipc_port *ipc_port_kernel_bootstrap;

// phase 5: BSD layer
extern void proc_init(void);

// phase 6: VFS
extern chimera_error_t vfs_init(void);
extern chimera_error_t vfs_build_root_hierarchy(void);

// phase 7: ChimeraKit driver framework
extern void chimera_kit_init(void);
extern void chimera_kit_start_matching(void);
extern void chimerakit_hid_init(void);

// phase 8: launchd-chimera
extern chimera_error_t launchd_chimera_start(void);

// phase 9: Scheduler
extern void scheduler_init(void);
extern CHIMERA_NORETURN void scheduler_run(void);

extern void gdt_init(void);
extern int mach_load(void *module_ptr, struct chimera_task *out_task,
                      uptr *entry_point, uptr *user_stack);
extern int mach_load_args(void *module_ptr, struct chimera_task *out_task,
                           uptr *entry_point, uptr *user_stack,
                           const char *arg0, char *const argv[], char *const envp[]);
extern CHIMERA_NORETURN void task_switch_to_user(uptr entry_point, uptr user_stack);

// boot info passed from the boot stub

typedef struct CHIMERA_PACKED chimera_boot_info {
  u64 magic;
  u32 version;
  u32 flags;

  chimera_paddr_t memmap_base;
  usize memmap_count;
  usize memmap_desc_size;

  chimera_paddr_t kernel_phys_start;
  chimera_paddr_t kernel_phys_end;
  chimera_vaddr_t kernel_virt_start;

  chimera_paddr_t fb_base;
  u32 fb_width;
  u32 fb_height;
  u32 fb_stride;
  u32 fb_format;
  u32 fb_bpp;
  u32 fb_pitch;

  chimera_paddr_t rsdp_base;
  chimera_paddr_t smbios_base;

  char cmdline[256];
} chimera_boot_info_t;

#define CHIMERA_BOOT_MAGIC UINT64_C(0x584955424F4F5421)

// limine requests
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0};

static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST, .revision = 0};

static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST, .revision = 0};

static volatile struct limine_kernel_address_request kernel_addr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST, .revision = 0};

static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST, .revision = 0};

static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST, .revision = 0};

static volatile struct limine_kernel_file_request kernel_file_request = {
    .id = LIMINE_KERNEL_FILE_REQUEST, .revision = 0};

volatile struct limine_smp_request smp_request = {.id = LIMINE_SMP_REQUEST,
                                                  .revision = 0};

// global boot info
chimera_boot_info_t g_boot_info_storage;
chimera_boot_info_t *g_boot_info = &g_boot_info_storage;

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
  serial_puts("  ║   Chimera Operating System  v0.1.0        ║\n");
  serial_puts("  ║   Stage 1 — Foundation Kernel         ║\n");
  serial_puts("  ║   Hybrid Mach/BSD Kernel               ║\n");
  serial_puts("  ╚═══════════════════════════════════════╝\n");
  serial_puts("\n");
}

// boot info validation
static void validate_boot_info(const chimera_boot_info_t *bi) {
  if (bi->magic != CHIMERA_BOOT_MAGIC) {
    chimera_panic("chimera_kernel_main: invalid boot info magic "
              "(got 0x%016llx, expected 0x%016llx)\n",
              (unsigned long long)bi->magic,
              (unsigned long long)CHIMERA_BOOT_MAGIC);
  }
  if (bi->memmap_count == 0 || bi->memmap_base == 0) {
    kprintf("[CHIMERA] Warning: No memory map from bootloader.\n");
  }
  // relaxing image bounds check for early Stage 1 Limine boot
}

// helper: log subsystem init step
static void init_log(const char *subsystem, bool ok) {
  if (ok) {
    kprintf("  [  OK  ]  %s\n", subsystem);
  } else {
    kprintf("  [ FAIL ]  %s\n", subsystem);
    chimera_panic("Kernel initialization failed in subsystem: %s\n", subsystem);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * chimera_kernel_main — The Primary Kernel Entry Point
 *
 * Called by the Limine bootloader with interrupts DISABLED.
 * ═══════════════════════════════════════════════════════════════════════════
 */
bool k_str_contains(const char *haystack, const char *needle) {
  if (!haystack || !needle) return false;
  if (!*needle) return true;
  for (usize i = 0; haystack[i]; i++) {
    usize j = 0;
    while (haystack[i + j] && needle[j] && haystack[i + j] == needle[j]) {
      j++;
    }
    if (!needle[j]) return true;
  }
  return false;
}

static bool spawn_user_process(const char *path, const char *name) {
  vnode_t *vp = nullptr;
  extern chimera_error_t vfs_lookup(const char *path, vnode_t **vp_out);
  if (vfs_lookup(path, &vp) != CHIMERA_SUCCESS || !vp) {
    return false;
  }

  void *elf_addr = vp->v_data;
  chimera_paddr_t temp_phys = (chimera_paddr_t)-1;
  usize temp_pages = 0;

  if (vp->v_op && __builtin_strcmp(vp->v_op->vop_name, "fat32_file") == 0) {
    typedef struct {
      u32 start_cluster;
      u32 file_size;
      bool is_dir;
      char path[256];
    } fat32_node_info_t;

    fat32_node_info_t *nd = (fat32_node_info_t *)vp->v_data;
    if (nd && nd->file_size > 0) {
      extern chimera_paddr_t pmm_alloc_pages(usize count);
      extern void pmm_free_contiguous(chimera_paddr_t base, usize count);
      temp_pages = (nd->file_size + 4095) / 4096;
      temp_phys = pmm_alloc_pages(temp_pages);
      if (temp_phys != (chimera_paddr_t)-1) {
        elf_addr = (void *)(temp_phys + g_hhdm_base);
        u32 actual = 0;
        extern chimera_error_t fat32_read_file(u32 start_cluster, u32 file_size,
                                           u32 offset, void *dst, u32 len,
                                           u32 *bytes_read);
        fat32_read_file(nd->start_cluster, nd->file_size, 0, elf_addr,
                        nd->file_size, &actual);
      }
    }
  }

  if (elf_addr) {
    uptr entry_point = 0;
    uptr user_stack = 0;

    chimera_proc_t *proc = nullptr;
    proc_create(proc_launchd, name, &proc);
    chimera_task_t *task = proc->p_task;

    extern void *kalloc(usize size);
    chimera_thread_t *thread = (chimera_thread_t *)kalloc(sizeof(chimera_thread_t));
    if (!thread) {
      static chimera_thread_t s_fallback_thread;
      thread = &s_fallback_thread;
    }
    __builtin_memset(thread, 0, sizeof(chimera_thread_t));
    thread->th_task = task;
    task->ta_threads = thread;
    thread->th_state = THREAD_STATE_READY;
    thread->th_priority = 0;

    const char *argv[] = { name, nullptr };
    const char *envp[] = {
        "HOME=/Users/root",
        "USER=root",
        "LOGNAME=root",
        "PATH=/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin",
        "TERM=xterm-256color",
        "SHELL=/bin/zsh",
        "PWD=/",
        "PROMPT=%n@%m %~ %# ",
        "PS1=%n@%m %~ %# ",
        nullptr
    };
    int load_rc = mach_load_args(elf_addr, task, &entry_point, &user_stack, path, (char *const *)argv, (char *const *)envp);
    if (load_rc != 0 || entry_point == 0) {
        kprintf("[CHIMERA] ERROR: Failed to load %s (%s): rc=%d\n", name, path, load_rc);
        if (temp_pages > 0) {
            extern void pmm_free_contiguous(chimera_paddr_t base, usize count);
            pmm_free_contiguous(temp_phys, temp_pages);
        }
        return false;
    }
    thread->th_context = (void *)entry_point;

    scheduler_add_thread(thread);

    extern void thread_init_stack(chimera_thread_t * th, void *entry, void *stack);
    thread_init_stack(thread, (void *)entry_point, (void *)user_stack);

    extern void scheduler_set_initial(chimera_thread_t * th);
    scheduler_set_initial(thread);
    kprintf("[CHIMERA] Spawned user process %s (%s) PID=%d\n", name, path, proc->p_pid);
  }

  if (temp_pages > 0) {
    extern void pmm_free_contiguous(chimera_paddr_t base, usize count);
    pmm_free_contiguous(temp_phys, temp_pages);
  }

  return (elf_addr != nullptr);
}

void chimera_kernel_main(chimera_boot_info_t *info) {
  // higher Half Direct Map base — MUST be initialized first!
  if (hhdm_request.response) {
    g_hhdm_base = hhdm_request.response->offset;
  } else {
    g_hhdm_base = 0xffff800000000000ULL;
  }

  // early console & framebuffer
  console_init();

  if (info != nullptr && info->magic == CHIMERA_BOOT_MAGIC) {
    __builtin_memcpy(g_boot_info, info, sizeof(chimera_boot_info_t));
    g_fb_phys_addr = g_boot_info->fb_base;
    kprintf("[CHIMERA] Kernel loaded via XIU EFI Bootloader\n");
  } else {
    // translate Limine info into chimera_boot_info_t
    g_boot_info->magic = CHIMERA_BOOT_MAGIC;

    if (framebuffer_request.response &&
        framebuffer_request.response->framebuffer_count > 0) {
      struct limine_framebuffer *fb =
          framebuffer_request.response->framebuffers[0];

      g_boot_info->fb_base = (chimera_paddr_t)((u64)fb->address - g_hhdm_base);
      g_boot_info->fb_width = fb->width;
      g_boot_info->fb_height = fb->height;
      g_boot_info->fb_bpp = fb->bpp;
      g_boot_info->fb_pitch = fb->pitch;
      g_boot_info->fb_stride = fb->pitch / (fb->bpp / 8);
      g_fb_phys_addr = g_boot_info->fb_base;
    }
    // translate Limine info into chimera_boot_info_t
    g_boot_info->magic = CHIMERA_BOOT_MAGIC;

    if (memmap_request.response) {
      g_boot_info->memmap_base = (chimera_paddr_t)memmap_request.response->entries;
      g_boot_info->memmap_count = memmap_request.response->entry_count;
    } else {
      kprintf("[CHIMERA] Warning: No memory map from bootloader.\n");
    }

    if (rsdp_request.response) {
      g_boot_info->rsdp_base = (chimera_paddr_t)rsdp_request.response->address;
    }

    if (smp_request.response) {
      g_cpu_count = (u32)smp_request.response->cpu_count;
      kprintf("[CHIMERA] SMP: Detected %u CPU(s)\n", g_cpu_count);
    }

    if (kernel_addr_request.response) {
      g_boot_info->kernel_phys_start =
          kernel_addr_request.response->physical_base;
      g_boot_info->kernel_virt_start = kernel_addr_request.response->virtual_base;
      g_boot_info->kernel_phys_end = g_boot_info->kernel_phys_start + 0x100000;
    }

    validate_boot_info(g_boot_info);
    kprintf("[CHIMERA] Kernel loaded via Limine Protocol\n");
  }

  if (g_boot_info->fb_base != 0) {
    extern void console_init_display(unsigned long baseaddr, uint64_t physaddr,
                                     unsigned int width, unsigned int height,
                                     unsigned int depth, unsigned int pitch);
    console_init_display((unsigned long)(g_boot_info->fb_base + g_hhdm_base), (uint64_t)g_boot_info->fb_base,
                         g_boot_info->fb_width, g_boot_info->fb_height, g_boot_info->fb_bpp, g_boot_info->fb_pitch);
  }

  serial_puts("[CHIMERA] Early boot — console active\n");
  print_banner();

  kprintf("[CHIMERA] Framebuffer: %ux%u @ phys=0x%llx\n", g_boot_info->fb_width,
          g_boot_info->fb_height, (unsigned long long)g_boot_info->fb_base);

  kprintf("\n[CHIMERA] === Kernel Initialization ===\n\n");

  kprintf("[CHIMERA] Initializing x86_64 Core (GDT, TSS, IDT)...\n");
  gdt_init();

  extern void idt_init(void);
  idt_init();

  kprintf("  [  OK  ]  Architecture GDT\n");

  // phase 1: Physical Memory Manager
  kprintf("[CHIMERA] Phase 1: Physical Memory Manager\n");
  pmm_init(g_boot_info->memmap_base, g_boot_info->memmap_count);
  video_console_init_backbuffer();
  kprintf("        Total RAM  : %zu MiB\n",
          (pmm_total_pages() * CHIMERA_PAGE_SIZE) / (1024 * 1024));
  kprintf("        Free pages : %zu (%zu MiB)\n", pmm_free_pages(),
          (pmm_free_pages() * CHIMERA_PAGE_SIZE) / (1024 * 1024));
  init_log("Physical Memory Manager", true);

  // phase 2: Virtual Memory
  kprintf("[CHIMERA] Phase 2: Virtual Memory Manager\n");
  pmap_bootstrap();
  init_log("Architecture PMAP", true);

  vm_object_init();
  init_log("VM Object subsystem", true);

  vm_map_init();
  init_log("VM Map subsystem", true);

  kprintf("[CHIMERA] Phase 3: Zone Allocator\n");
  zone_init();
  init_log("Kernel zone allocator", true);

  kprintf("[CHIMERA] Initializing Symmetric Multiprocessing (SMP)...\n");
  extern void smp_init(void);
  smp_init();

  kprintf("[CHIMERA] Phase 4: Mach IPC subsystem\n");

  extern void task_init(void);
  task_init();
  proc_init();
  CHIMERA_ASSERT(task_kernel != NULL);
  CHIMERA_ASSERT(proc_kernel != NULL);
  init_log("Mach Task/Proc foundation", true);

  ipc_init();
  CHIMERA_ASSERT(ipc_port_kernel_bootstrap != NULL);
  init_log("Mach IPC (ports, spaces, kmsg)", true);

  kprintf("[CHIMERA] Phase 5: BSD Personality Layer\n");
  kprintf("        kernel task  : id=0x%llx\n",
          (unsigned long long)task_kernel->ta_id);
  kprintf("        kernel proc  : pid=%u\n", proc_kernel->p_pid);
  init_log("BSD personality", true);

  // phase 6: Virtual File System
  kprintf("[CHIMERA] Phase 6: Virtual File System\n");
  chimera_error_t err = vfs_init();
  init_log("VFS core & DevFS", CHIMERA_SUCCEEDED(err));

  kprintf("        Root vnode   : %s  type=VDIR\n",
          vfs_root_vnode ? vfs_root_vnode->v_name : "(null)");

  // phase 7: ChimeraKit Driver Framework
  kprintf("[CHIMERA] Phase 7: ChimeraKit Driver Framework\n");

  // enumerate PCI Bus
  chimerakit_pci_init();

  extern void chimerakit_hid_init(void);
  chimerakit_hid_init();

  chimera_kit_init();
  chimera_kit_start_matching();
  init_log("ChimeraKit driver registry", true);
  init_log("Driver matching pass", true);

  kprintf("[CHIMERA] Phase 7.5: Persistent Storage & FAT32 Filesystem\n");
  extern chimera_error_t ata_init(void);
  extern chimera_error_t fat32_init(void);
  ata_init();
  fat32_init();

  // phase 7.8: chimera-net darwin bsd network subsystem
  extern void net_init(void);
  net_init();

  kprintf("[CHIMERA] Phase 8: launchd-chimera\n");
  err = launchd_chimera_start();
  init_log("launchd-chimera (PID 1)", CHIMERA_SUCCEEDED(err));

  if (CHIMERA_SUCCEEDED(err)) {
    kprintf("        launchd PID  : %u\n",
            proc_launchd ? proc_launchd->p_pid : 0u);
  }

  // phase 9: scheduler
  kprintf("\n[CHIMERA] Phase 9: Starting scheduler\n");
  scheduler_init();

  if (module_request.response && module_request.response->module_count > 0) {
    kprintf("[CHIMERA] Found %llu in-memory module(s).\n",
            (unsigned long long)module_request.response->module_count);
    for (u64 i = 0; i < module_request.response->module_count; i++) {
      struct limine_file *module = module_request.response->modules[i];
      const char *mpath = (module->cmdline && module->cmdline[0] != '\0')
                              ? module->cmdline
                              : module->path;
      extern chimera_error_t vfs_register_module(const char *path, void *addr,
                                             usize size);
      vfs_register_module(mpath, module->address, module->size);
    }
  }

  extern void chimerakit_xhci_dump_status(void);
  chimerakit_xhci_dump_status();

  bool auto_start_gui = false;
  if (kernel_file_request.response && kernel_file_request.response->kernel_file) {
    const char *cmd = kernel_file_request.response->kernel_file->cmdline;
    if (cmd && cmd[0] != '\0') {
      __builtin_strncpy(g_boot_info->cmdline, cmd, sizeof(g_boot_info->cmdline) - 1);
      extern bool k_str_contains(const char *haystack, const char *needle);
      if (k_str_contains(cmd, "wserver") || k_str_contains(cmd, "gui") ||
          k_str_contains(cmd, "-wserver")) {
        auto_start_gui = true;
      }
    }
  }

  if (auto_start_gui) {
    kprintf("[CHIMERA] GUI boot flag (-wserver) detected. Launching WindowServer...\n");
    bool ws_started = spawn_user_process("/System/Library/CoreServices/WindowServer", "WindowServer");
    if (!ws_started) ws_started = spawn_user_process("/usr/sbin/WindowServer", "WindowServer");
    if (!ws_started) ws_started = spawn_user_process("/usr/bin/WindowServer", "WindowServer");
    if (!ws_started) ws_started = spawn_user_process("/bin/wserver", "wserver");

    if (ws_started) {
      kprintf("[CHIMERA] WindowServer started successfully as primary GUI compositor.\n");
    } else {
      kprintf("[CHIMERA] WARNING: WindowServer binary not found! Falling back to zsh.\n");
      if (!spawn_user_process("/bin/zsh", "zsh")) {
        spawn_user_process("/bin/sh", "sh");
      }
    }
  } else {
    // Console boot mode (interactive zsh)
    if (!spawn_user_process("/bin/zsh", "zsh")) {
      if (!spawn_user_process("/bin/sh", "sh")) {
        kprintf("[CHIMERA] WARNING: /bin/zsh and /bin/sh not found in VFS or on disk!\n");
      }
    }
  }

  /*
   * scheduler_run() enables interrupts and drops into the idle thread.
   * It must never return.
   */
  scheduler_run();

  // unreachable — satisfy noreturn path for non-[[noreturn]] callers
  CHIMERA_UNREACHABLE();
}

/* ── Kernel idle entry (lowest-priority thread) ─────────────────────────
 * The scheduler calls this function in the idle thread context when no
 * other thread is runnable.  On x86_64 we issue HLT to save power.
 */
CHIMERA_USED CHIMERA_COLD static void chimera_idle_loop(void) {
  for (;;) {
#if defined(CHIMERA_ARCH_x86_64)
    __asm__ volatile("sti; hlt" ::: "memory");
#elif defined(CHIMERA_ARCH_arm64)
    __asm__ volatile("wfi" ::: "memory");
#else
    cpu_relax();
#endif
  }
}
