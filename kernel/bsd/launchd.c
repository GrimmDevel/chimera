// launchd pid 1
#include <kernel/launchd.h>
#include <kernel/proc.h>
#include <kernel/panic.h>

xiu_error_t launchd_xiu_start(void) {
    kprintf("        launchd-xiu: initializing PID 1 foundation...\n");

    xiu_error_t err = proc_create(proc_kernel, "launchd", &proc_launchd);
    if (XIU_FAILED(err)) {
        xiu_panic("launchd_xiu_start: failed to create PID 1 (err=%d)\n", err);
    }

    XIU_ASSERT(proc_launchd->p_pid == 1);
    
    kprintf("        launchd-xiu: PID 1 active (task_id=0x%llx)\n",
            (unsigned long long)proc_launchd->p_task->ta_id);

    return XIU_SUCCESS;
}
