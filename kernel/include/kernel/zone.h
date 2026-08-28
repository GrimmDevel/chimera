/* =============================================================================
 * Chimera Operating System — Darwin Mach Zone Allocator Interface
 * kernel/include/kernel/zone.h
 * ============================================================================= */

#ifndef _KERNEL_ZONE_H_
#define _KERNEL_ZONE_H_

#include <mach/vm_types.h>
#include <kernel/spinlock.h>
#include <kernel/chimera_types.h>

struct zone;
typedef struct zone *zone_t;
#define ZONE_NULL ((zone_t) 0)

typedef struct zone {
    const char         *z_name;
    vm_size_t           z_elem_size;
    vm_size_t           z_alloc_size;
    vm_size_t           z_max_size;
    vm_size_t           z_cur_size;
    u32                 z_count;
    u32                 z_free_count;
    void               *z_free_list;
    spinlock_t          z_lock;
} zone_struct_t;

void    zone_init(void);
zone_t  zinit(vm_size_t size, vm_size_t max, vm_size_t alloc, const char *name);
zone_t  zone_create(const char *name, vm_size_t size, vm_size_t flags);
void   *zalloc(zone_t zone);
void   *zalloc_noblock(zone_t zone);
void    zfree(zone_t zone, void *elem);

void   *kalloc(usize size);
void    kfree(void *ptr);

#endif /* _KERNEL_ZONE_H_ */
