/* =============================================================================
 * Chimera Operating System — Darwin Mach Physical Page Descriptor Interface
 * kernel/include/kernel/vm_page.h
 * ============================================================================= */

#ifndef _KERNEL_VM_PAGE_H_
#define _KERNEL_VM_PAGE_H_

#include <mach/vm_types.h>
#include <kernel/chimera_types.h>
#include <kernel/spinlock.h>

#define VM_PAGE_FREE        0x0001
#define VM_PAGE_ACTIVE      0x0002
#define VM_PAGE_INACTIVE    0x0004
#define VM_PAGE_WIRED       0x0008
#define VM_PAGE_COW         0x0010

typedef struct vm_page {
    struct vm_page *next;
    struct vm_page *prev;
    ppnum_t         phys_page;
    u16             ref_count;
    u16             wire_count;
    u16             flags;
    u16             order;
} vm_page_t;

#define VM_PAGE_NULL ((vm_page_t *)0)

#endif /* _KERNEL_VM_PAGE_H_ */
