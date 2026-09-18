// launchd pid 1
#include <kernel/launchd.h>
#include <kernel/proc.h>
#include <kernel/panic.h>
#include <kernel/vfs_node.h>

extern void kprintf(const char *fmt, ...);
extern chimera_error_t vfs_lookup(const char *path, vnode_t **vp_out);
extern int mach_load_args(void *module_ptr, struct chimera_task *out_task,
                          uptr *entry_point, uptr *user_stack,
                          const char *arg0, char *const argv[],
                          char *const envp[]);
extern chimera_paddr_t pmm_alloc_pages(usize count);
extern void pmm_free_contiguous(chimera_paddr_t base, usize count);
extern void thread_init_stack(chimera_thread_t *th, void *entry, void *stack);
extern int scheduler_add_thread(chimera_thread_t *th);
extern void *kalloc(usize size);
extern u64 g_hhdm_base;

// set when PID 1 runs the real userland init (kernel_main skips spawning a
// bare shell in that case — launchd supervises the console shell itself)
bool g_launchd_userland = false;

chimera_error_t launchd_chimera_start(void) {
    kprintf("        launchd-chimera: initializing PID 1 foundation...\n");

    chimera_error_t err = proc_create(proc_kernel, "launchd", &proc_launchd);
    if (CHIMERA_FAILED(err)) {
        chimera_panic("launchd_chimera_start: failed to create PID 1 (err=%d)\n", err);
    }

    CHIMERA_ASSERT(proc_launchd->p_pid == 1);

    kprintf("        launchd-chimera: PID 1 active (task_id=0x%llx)\n",
            (unsigned long long)proc_launchd->p_task->ta_id);

    // ── load the userland init binary into PID 1 ──
    vnode_t *vp = nullptr;
    if (vfs_lookup("/bin/launchd", &vp) != CHIMERA_SUCCESS || !vp) {
        kprintf("        launchd-chimera: /bin/launchd not found — "
                "kernel-stub PID 1 (services must be spawned manually)\n");
        return CHIMERA_SUCCESS; // degrade to the old kernel-stub behaviour
    }

    void *bin = vp->v_data;
    chimera_paddr_t temp_phys = (chimera_paddr_t)-1;
    usize temp_pages = 0;

    if (vp->v_op && __builtin_strcmp(vp->v_op->vop_name, "fat32_file") == 0) {
        typedef struct {
            u32 start_cluster;
            u32 file_size;
        } fat32_head_t;
        fat32_head_t *nd = (fat32_head_t *)vp->v_data;
        if (!nd || nd->file_size == 0) {
            kprintf("        launchd-chimera: empty /bin/launchd — kernel stub\n");
            return CHIMERA_SUCCESS;
        }
        temp_pages = (nd->file_size + 4095) / 4096;
        temp_phys = pmm_alloc_pages(temp_pages);
        if (temp_phys == (chimera_paddr_t)-1 || temp_phys == 0) {
            kprintf("        launchd-chimera: OOM reading /bin/launchd — kernel stub\n");
            return CHIMERA_SUCCESS;
        }
        bin = (void *)(temp_phys + g_hhdm_base);
        extern chimera_error_t fat32_read_file(u32 start_cluster, u32 file_size,
                                               u32 offset, void *dst, u32 len,
                                               u32 *bytes_read);
        u32 actual = 0;
        err = fat32_read_file(nd->start_cluster, nd->file_size, 0, bin,
                              nd->file_size, &actual);
        if (err != CHIMERA_SUCCESS || actual == 0) {
            pmm_free_contiguous(temp_phys, temp_pages);
            kprintf("        launchd-chimera: read failed — kernel stub\n");
            return CHIMERA_SUCCESS;
        }
    }

    chimera_task_t *task = proc_launchd->p_task;
    uptr entry = 0, user_stack = 0;
    const char *argv[] = {"launchd", nullptr};
    const char *envp[] = {"TERM=xterm-256color", "PATH=/bin:/usr/bin:/sbin",
                          "HOME=/Users/root", "USER=root", nullptr};
    int rc = mach_load_args(bin, task, &entry, &user_stack, "/bin/launchd",
                            (char *const *)argv, (char *const *)envp);
    if (temp_pages > 0) pmm_free_contiguous(temp_phys, temp_pages);
    if (rc != 0 || entry == 0) {
        kprintf("        launchd-chimera: mach_load failed (%d) — kernel stub\n", rc);
        return CHIMERA_SUCCESS;
    }

    chimera_thread_t *th = (chimera_thread_t *)kalloc(sizeof(chimera_thread_t));
    if (!th) {
        kprintf("        launchd-chimera: OOM for init thread — kernel stub\n");
        return CHIMERA_SUCCESS;
    }
    __builtin_memset(th, 0, sizeof(chimera_thread_t));
    th->th_signature = CHIMERA_THREAD_MAGIC;
    th->th_task = task;
    task->ta_threads = th;
    th->th_state = THREAD_STATE_READY;
    th->th_priority = 32;

    thread_init_stack(th, (void *)entry, (void *)user_stack);
    if (scheduler_add_thread(th) == 0) {
        g_launchd_userland = true;
        kprintf("        launchd-chimera: userland init scheduled "
                "(entry=0x%llx)\n",
                (unsigned long long)entry);
    } else {
        th->th_signature = 0;
        kprintf("        launchd-chimera: scheduler refused init thread\n");
    }

    return CHIMERA_SUCCESS;
}
