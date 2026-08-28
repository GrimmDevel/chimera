// launchd pid 1
#include <kernel/launchd.h>
#include <kernel/proc.h>
#include <kernel/panic.h>

chimera_error_t launchd_chimera_start(void) {
    kprintf("        launchd-chimera: initializing PID 1 foundation...\n");

    chimera_error_t err = proc_create(proc_kernel, "launchd", &proc_launchd);
    if (CHIMERA_FAILED(err)) {
        chimera_panic("launchd_chimera_start: failed to create PID 1 (err=%d)\n", err);
    }

    CHIMERA_ASSERT(proc_launchd->p_pid == 1);
    
    kprintf("        launchd-chimera: PID 1 active (task_id=0x%llx)\n",
            (unsigned long long)proc_launchd->p_task->ta_id);

    return CHIMERA_SUCCESS;
}
