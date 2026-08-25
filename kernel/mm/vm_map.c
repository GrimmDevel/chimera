/* =============================================================================
 * XIU Operating System — Darwin Mach Virtual Memory Map Subsystem
 * kernel/mm/vm_map.c
 *
 * Implements address space management (vm_map_t) using doubly linked ranges
 * of vm_map_entry_t. Handles mmap, munmap, mprotect, and fork address space cloning.
 * ============================================================================= */

#include <kernel/vm_map.h>
#include <kernel/zone.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/xiu_types.h>

extern void kprintf(const char *fmt, ...);
extern u64  g_hhdm_base;
extern void pmap_unmap_user_range(u64 pml4_phys, u64 vaddr, usize len);
extern int  pmap_protect_user_range(u64 pml4_phys, u64 virt_start, usize len, u32 prot);

static zone_t s_vm_map_zone = ZONE_NULL;
static zone_t s_vm_map_entry_zone = ZONE_NULL;
static bool   s_vm_map_initialized = false;

void vm_map_init(void) {
    if (s_vm_map_initialized) return;

    s_vm_map_zone = zinit(sizeof(vm_map_struct_t), 0, 4096, "vm_maps");
    s_vm_map_entry_zone = zinit(sizeof(vm_map_entry_t), 0, 4096, "vm_map_entries");
    s_vm_map_initialized = true;

    kprintf("        vm_map: Darwin Mach VM Map subsystem initialized\n");
}

vm_map_t vm_map_create(pmap_t pmap, vm_offset_t min, vm_offset_t max) {
    if (!s_vm_map_initialized) {
        vm_map_init();
    }

    vm_map_t map = (vm_map_t)zalloc(s_vm_map_zone);
    if (!map) return VM_MAP_NULL;

    __builtin_memset(map, 0, sizeof(*map));
    map->vmm_pmap = pmap;
    map->vmm_min_offset = min;
    map->vmm_max_offset = max;
    map->vmm_ref_count = 1;
    map->vmm_nentries = 0;
    map->vmm_header = nullptr;
    spinlock_init(&map->vmm_lock);

    return map;
}

void vm_map_reference(vm_map_t map) {
    if (!map) return;
    irq_flags_t irq = spinlock_lock_irqsave(&map->vmm_lock);
    map->vmm_ref_count++;
    spinlock_unlock_irqrestore(&map->vmm_lock, irq);
}

void vm_map_destroy(vm_map_t map) {
    if (!map) return;

    irq_flags_t irq = spinlock_lock_irqsave(&map->vmm_lock);
    vm_map_entry_t *entry = map->vmm_header;
    while (entry) {
        vm_map_entry_t *next = entry->vme_next;
        if (entry->vme_object) {
            vm_object_deallocate(entry->vme_object);
        }
        zfree(s_vm_map_entry_zone, entry);
        entry = next;
    }
    map->vmm_header = nullptr;
    map->vmm_nentries = 0;
    spinlock_unlock_irqrestore(&map->vmm_lock, irq);

    zfree(s_vm_map_zone, map);
}

void vm_map_deallocate(vm_map_t map) {
    if (!map) return;

    irq_flags_t irq = spinlock_lock_irqsave(&map->vmm_lock);
    if (map->vmm_ref_count > 0) {
        map->vmm_ref_count--;
        if (map->vmm_ref_count == 0) {
            spinlock_unlock_irqrestore(&map->vmm_lock, irq);
            vm_map_destroy(map);
            return;
        }
    }
    spinlock_unlock_irqrestore(&map->vmm_lock, irq);
}

xiu_error_t vm_map_enter(vm_map_t map, vm_offset_t *address, vm_size_t size,
                         vm_offset_t mask, int flags, vm_object_t object,
                         vm_object_offset_t offset, bool copy,
                         vm_prot_t cur_protection, vm_prot_t max_protection,
                         vm_inherit_t inheritance) {
    (void)mask; (void)flags; (void)copy;
    if (!map || !address || size == 0) return XIU_ERR_INVALID;

    size = (size + 4095) & ~4095ULL;
    vm_offset_t start = *address;

    irq_flags_t irq = spinlock_lock_irqsave(&map->vmm_lock);

    // auto-allocate address range if 0 specified
    if (start == 0) {
        start = 0x90000000ULL; // standard user dynamic mmap region
        vm_map_entry_t *cur = map->vmm_header;
        while (cur) {
            if (start < cur->vme_end && (start + size) > cur->vme_start) {
                start = (cur->vme_end + 4095) & ~4095ULL;
            }
            cur = cur->vme_next;
        }
        *address = start;
    }

    vm_offset_t end = start + size;

    vm_map_entry_t *entry = (vm_map_entry_t *)zalloc(s_vm_map_entry_zone);
    if (!entry) {
        spinlock_unlock_irqrestore(&map->vmm_lock, irq);
        return XIU_ERR_NOMEM;
    }

    __builtin_memset(entry, 0, sizeof(*entry));
    entry->vme_start = start;
    entry->vme_end = end;
    entry->vme_object = object;
    entry->vme_offset = offset;
    entry->vme_prot = cur_protection;
    entry->vme_max_prot = max_protection;
    entry->vme_inheritance = inheritance;

    if (object) {
        vm_object_reference(object);
    }

    // insert entry into sorted linked list
    vm_map_entry_t *prev = nullptr;
    vm_map_entry_t *curr = map->vmm_header;
    while (curr && curr->vme_start < start) {
        prev = curr;
        curr = curr->vme_next;
    }

    entry->vme_prev = prev;
    entry->vme_next = curr;

    if (prev) {
        prev->vme_next = entry;
    } else {
        map->vmm_header = entry;
    }
    if (curr) {
        curr->vme_prev = entry;
    }

    map->vmm_nentries++;
    map->vmm_size += size;

    spinlock_unlock_irqrestore(&map->vmm_lock, irq);
    return XIU_SUCCESS;
}

xiu_error_t vm_map_remove(vm_map_t map, vm_offset_t start, vm_offset_t end) {
    if (!map || start >= end) return XIU_ERR_INVALID;

    start = start & ~4095ULL;
    end = (end + 4095) & ~4095ULL;

    irq_flags_t irq = spinlock_lock_irqsave(&map->vmm_lock);

    vm_map_entry_t *curr = map->vmm_header;
    while (curr) {
        vm_map_entry_t *next = curr->vme_next;

        if (curr->vme_start >= start && curr->vme_end <= end) {
            // full overlap - remove entry
            if (curr->vme_prev) {
                curr->vme_prev->vme_next = curr->vme_next;
            } else {
                map->vmm_header = curr->vme_next;
            }
            if (curr->vme_next) {
                curr->vme_next->vme_prev = curr->vme_prev;
            }

            if (map->vmm_size >= (curr->vme_end - curr->vme_start)) {
                map->vmm_size -= (curr->vme_end - curr->vme_start);
            }
            map->vmm_nentries--;

            if (curr->vme_object) {
                vm_object_deallocate(curr->vme_object);
            }
            zfree(s_vm_map_entry_zone, curr);
        }

        curr = next;
    }

    if (map->vmm_pmap) {
        pmap_unmap_user_range((u64)map->vmm_pmap, start, end - start);
    }

    spinlock_unlock_irqrestore(&map->vmm_lock, irq);
    return XIU_SUCCESS;
}

xiu_error_t vm_map_protect(vm_map_t map, vm_offset_t start, vm_offset_t end,
                           vm_prot_t new_prot, bool set_max) {
    if (!map || start >= end) return XIU_ERR_INVALID;

    start = start & ~4095ULL;
    end = (end + 4095) & ~4095ULL;

    irq_flags_t irq = spinlock_lock_irqsave(&map->vmm_lock);

    vm_map_entry_t *curr = map->vmm_header;
    while (curr) {
        if (curr->vme_start < end && curr->vme_end > start) {
            if (set_max) {
                curr->vme_max_prot = new_prot;
            } else {
                curr->vme_prot = new_prot;
            }
        }
        curr = curr->vme_next;
    }

    if (map->vmm_pmap) {
        pmap_protect_user_range((u64)map->vmm_pmap, start, end - start, (u32)new_prot);
    }

    spinlock_unlock_irqrestore(&map->vmm_lock, irq);
    return XIU_SUCCESS;
}

vm_map_t vm_map_fork(vm_map_t src_map) {
    if (!src_map) return VM_MAP_NULL;

    vm_map_t dst_map = vm_map_create(nullptr, src_map->vmm_min_offset, src_map->vmm_max_offset);
    if (!dst_map) return VM_MAP_NULL;

    irq_flags_t irq = spinlock_lock_irqsave(&src_map->vmm_lock);

    vm_map_entry_t *src_entry = src_map->vmm_header;
    while (src_entry) {
        if (src_entry->vme_inheritance != VM_INHERIT_NONE) {
            vm_offset_t addr = src_entry->vme_start;
            vm_size_t size = src_entry->vme_end - src_entry->vme_start;

            vm_map_enter(dst_map, &addr, size, 0, 0,
                         src_entry->vme_object, src_entry->vme_offset,
                         (src_entry->vme_inheritance == VM_INHERIT_COPY),
                         src_entry->vme_prot, src_entry->vme_max_prot,
                         src_entry->vme_inheritance);
        }
        src_entry = src_entry->vme_next;
    }

    spinlock_unlock_irqrestore(&src_map->vmm_lock, irq);
    return dst_map;
}
