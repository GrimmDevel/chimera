/* =============================================================================
 * Chimera Operating System — Mach Task Management
 * kernel/mach/task.c
 * =============================================================================
 */

#include <kernel/proc.h>
#include <kernel/spinlock.h>

chimera_task_t *task_kernel = nullptr;
static chimera_task_t s_kernel_task_obj;

void task_init(void) {
    task_kernel = &s_kernel_task_obj;
    __builtin_memset(task_kernel, 0, sizeof(chimera_task_t));
    task_kernel->ta_signature = CHIMERA_TASK_MAGIC;
    task_kernel->ta_id = 0;
    task_kernel->ta_flags = TASK_FLAG_KERNEL | TASK_FLAG_64BIT;
    spinlock_init(&task_kernel->ta_lock);
}
