/* =============================================================================
 * XIU Operating System — Darwin Mach Virtual Memory Object Subsystem
 * kernel/mm/vm_object.c
 *
 * Implements Darwin vm_object_t backing, COW shadow chains, and memory faulting.
 * ============================================================================= */

#include <kernel/vm_object.h>
#include <kernel/zone.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/xiu_types.h>

extern void kprintf(const char *fmt, ...);
extern xiu_paddr_t pmm_alloc_page(void);
extern void pmm_release_page(xiu_paddr_t addr);
extern u64 g_hhdm_base;

static zone_t s_vm_obj_zone = ZONE_NULL;
static bool   s_vm_obj_initialized = false;

void vm_object_init(void) {
    if (s_vm_obj_initialized) return;

    s_vm_obj_zone = zinit(sizeof(vm_object_struct_t), 0, 4096, "vm_objects");
    s_vm_obj_initialized = true;

    kprintf("        vm_object: Darwin Mach VM Object subsystem initialized\n");
}

xiu_error_t vm_object_create(xiu_size_t size, vm_prot_t max_prot, vm_object_t *obj_out) {
    if (!obj_out) return XIU_ERR_INVALID;

    if (!s_vm_obj_initialized) {
        vm_object_init();
    }

    vm_object_t obj = (vm_object_t)zalloc(s_vm_obj_zone);
    if (!obj) return XIU_ERR_NOMEM;

    __builtin_memset(obj, 0, sizeof(*obj));
    obj->vmo_signature   = XIU_VM_OBJECT_MAGIC;
    obj->vmo_size        = size;
    obj->vmo_max_prot    = max_prot;
    obj->vmo_inherit     = VM_INHERIT_DEFAULT;
    obj->vmo_flags       = VM_OBJ_INTERNAL | VM_OBJ_ALIVE;
    obj->vmo_pager_type  = VM_PAGER_ANON;
    obj->vmo_ref_count   = 1;
    spinlock_init(&obj->vmo_lock);

    *obj_out = obj;
    return XIU_SUCCESS;
}

xiu_error_t vm_object_create_vnode(struct vnode *vp, xiu_offset_t offset,
                                   xiu_size_t size, vm_object_t *obj_out) {
    if (!vp || !obj_out) return XIU_ERR_INVALID;
    xiu_error_t err = vm_object_create(size, VM_PROT_ALL, obj_out);
    if (XIU_FAILED(err)) return err;

    vm_object_t obj = *obj_out;
    obj->vmo_pager_type   = VM_PAGER_VNODE;
    obj->vmo_vnode        = vp;
    obj->vmo_pager_offset = offset;
    obj->vmo_flags        = VM_OBJ_VNODE | VM_OBJ_ALIVE;
    return XIU_SUCCESS;
}

xiu_error_t vm_object_shadow(vm_object_t original, xiu_offset_t offset,
                             xiu_size_t size, vm_object_t *shadow_out) {
    if (!original || !shadow_out) return XIU_ERR_INVALID;

    vm_object_t shadow = nullptr;
    xiu_error_t err = vm_object_create(size, original->vmo_max_prot, &shadow);
    if (XIU_FAILED(err)) return err;

    vm_object_reference(original);
    shadow->vmo_shadow        = original;
    shadow->vmo_shadow_offset = offset;
    shadow->vmo_shadow_count  = 1;
    shadow->vmo_flags        |= VM_OBJ_SHADOW | VM_OBJ_COPY_ON_WRITE;

    *shadow_out = shadow;
    return XIU_SUCCESS;
}

void vm_object_reference(vm_object_t obj) {
    if (!obj || obj->vmo_signature != XIU_VM_OBJECT_MAGIC) return;
    atomic_fetch_add(&obj->vmo_ref_count, 1);
}

void vm_object_deallocate(vm_object_t obj) {
    if (!obj || obj->vmo_signature != XIU_VM_OBJECT_MAGIC) return;

    if (atomic_fetch_sub(&obj->vmo_ref_count, 1) == 1) {
        irq_flags_t flags = spinlock_lock_irqsave(&obj->vmo_lock);
        obj->vmo_flags &= ~VM_OBJ_ALIVE;

        // free resident pages
        vm_page_t *p = obj->vmo_pages;
        while (p) {
            vm_page_t *next = p->vmp_next;
            if (p->vmp_phys_addr && !(p->vmp_flags & VM_PAGE_FICTITIOUS)) {
                pmm_release_page(p->vmp_phys_addr);
            }
            kfree(p);
            p = next;
        }
        obj->vmo_pages = nullptr;
        obj->vmo_page_count = 0;

        vm_object_t shadow = obj->vmo_shadow;
        obj->vmo_shadow = nullptr;
        spinlock_unlock_irqrestore(&obj->vmo_lock, flags);

        if (shadow) {
            vm_object_deallocate(shadow);
        }

        obj->vmo_signature = 0;
        zfree(s_vm_obj_zone, obj);
    }
}

vm_page_t *vm_object_lookup_page(vm_object_t obj, xiu_offset_t offset) {
    if (!obj || obj->vmo_signature != XIU_VM_OBJECT_MAGIC) return nullptr;

    irq_flags_t flags = spinlock_lock_irqsave(&obj->vmo_lock);
    vm_page_t *curr = obj->vmo_pages;
    while (curr) {
        if (curr->vmp_offset == offset) {
            spinlock_unlock_irqrestore(&obj->vmo_lock, flags);
            return curr;
        }
        curr = curr->vmp_next;
    }
    spinlock_unlock_irqrestore(&obj->vmo_lock, flags);
    return nullptr;
}

vm_page_t *vm_object_resolve_shadow(vm_object_t obj, xiu_offset_t offset) {
    vm_object_t curr = obj;
    xiu_offset_t cur_off = offset;

    while (curr) {
        vm_page_t *p = vm_object_lookup_page(curr, cur_off);
        if (p) return p;
        cur_off += curr->vmo_shadow_offset;
        curr = curr->vmo_shadow;
    }
    return nullptr;
}

xiu_error_t vm_object_fault(vm_object_t obj, xiu_offset_t offset,
                            vm_prot_t fault_type, vm_page_t **page_out) {
    if (!obj || !page_out) return XIU_ERR_INVALID;
    (void)fault_type;

    vm_page_t *page = vm_object_lookup_page(obj, offset);
    if (!page) {
        vm_page_t *shadow_page = vm_object_resolve_shadow(obj, offset);

        xiu_paddr_t phys = pmm_alloc_page();
        if (phys == (xiu_paddr_t)-1 || phys == 0) return XIU_ERR_NOMEM;

        void *virt = (void *)(phys + g_hhdm_base);
        if (shadow_page && shadow_page->vmp_phys_addr) {
            __builtin_memcpy(virt, (void *)(shadow_page->vmp_phys_addr + g_hhdm_base), 4096);
        } else {
            __builtin_memset(virt, 0, 4096);
        }

        page = (vm_page_t *)kalloc(sizeof(vm_page_t));
        if (!page) {
            pmm_release_page(phys);
            return XIU_ERR_NOMEM;
        }

        __builtin_memset(page, 0, sizeof(*page));
        page->vmp_phys_addr = phys;
        page->vmp_object    = obj;
        page->vmp_offset    = offset;
        page->vmp_flags     = VM_PAGE_PMAPPED;

        irq_flags_t flags = spinlock_lock_irqsave(&obj->vmo_lock);
        page->vmp_next  = obj->vmo_pages;
        obj->vmo_pages  = page;
        obj->vmo_page_count++;
        obj->vmo_faults++;
        spinlock_unlock_irqrestore(&obj->vmo_lock, flags);
    }

    *page_out = page;
    return XIU_SUCCESS;
}

xiu_error_t vm_object_sync(vm_object_t obj) {
    if (!obj || obj->vmo_signature != XIU_VM_OBJECT_MAGIC) return XIU_ERR_INVALID;
    return XIU_SUCCESS;
}
