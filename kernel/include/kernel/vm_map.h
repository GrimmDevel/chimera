/* =============================================================================
 * Chimera Operating System — Darwin Mach Virtual Memory Map Interface
 * kernel/include/kernel/vm_map.h
 * ============================================================================= */

#ifndef _KERNEL_VM_MAP_H_
#define _KERNEL_VM_MAP_H_

#include <mach/vm_types.h>
#include <mach/vm_prot.h>
#include <mach/vm_inherit.h>
#include <kernel/spinlock.h>
#include <kernel/chimera_types.h>
#include <kernel/vm_object.h>

typedef struct vm_map_entry {
    struct vm_map_entry *vme_prev;
    struct vm_map_entry *vme_next;
    vm_offset_t          vme_start;
    vm_offset_t          vme_end;
    vm_object_t          vme_object;
    vm_object_offset_t   vme_offset;
    vm_prot_t            vme_prot;
    vm_prot_t            vme_max_prot;
    vm_inherit_t         vme_inheritance;
    u16                  vme_flags;
} vm_map_entry_t;

typedef struct _vm_map {
    vm_map_entry_t      *vmm_header;
    pmap_t               vmm_pmap;
    vm_size_t            vmm_size;
    u32                  vmm_nentries;
    spinlock_t           vmm_lock;
    u32                  vmm_ref_count;
    vm_offset_t          vmm_min_offset;
    vm_offset_t          vmm_max_offset;
} vm_map_struct_t;

void        vm_map_init(void);
vm_map_t    vm_map_create(pmap_t pmap, vm_offset_t min, vm_offset_t max);
void        vm_map_destroy(vm_map_t map);
void        vm_map_reference(vm_map_t map);
void        vm_map_deallocate(vm_map_t map);

chimera_error_t vm_map_enter(vm_map_t map, vm_offset_t *address, vm_size_t size,
                         vm_offset_t mask, int flags, vm_object_t object,
                         vm_object_offset_t offset, bool copy,
                         vm_prot_t cur_protection, vm_prot_t max_protection,
                         vm_inherit_t inheritance);

chimera_error_t vm_map_remove(vm_map_t map, vm_offset_t start, vm_offset_t end);
chimera_error_t vm_map_protect(vm_map_t map, vm_offset_t start, vm_offset_t end,
                           vm_prot_t new_prot, bool set_max);
vm_map_t    vm_map_fork(vm_map_t src_map);

#endif /* _KERNEL_VM_MAP_H_ */
