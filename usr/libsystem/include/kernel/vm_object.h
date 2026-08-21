/* =============================================================================
 * XIU Operating System — Virtual Memory Object
 * kernel/include/kernel/vm_object.h
 *
 * A vm_object_t is the fundamental unit of the XIU Virtual Memory Manager.
 * It represents a range of memory with a uniform pager — whether backed by:
 *   - Physical RAM (anonymous / swap-backed)
 *   - A file vnode (file-backed, for mmap and exec)
 *   - A device driver (MMIO regions)
 *
 * Shadowing:
 *   Objects form a shadow chain for copy-on-write semantics.  When a process
 *   forks, the child's vm_map_entry points to a NEW shadow object backed by
 *   the parent's object.  On first write, the page is copied into the shadow.
 *
 *   [shadow_object] → [parent_object] → [NULL]
 *
 * This is architecturally equivalent to XNU's vm_object / vm_object_shadow.
 * ============================================================================= */

#pragma once
#ifndef XIU_VM_OBJECT_H
#define XIU_VM_OBJECT_H

#include <kernel/xiu_types.h>
#include <kernel/spinlock.h>

// forward declarations
struct vm_object;
struct vm_page;
struct vnode;

/* ═══════════════════════════════════════════════════════════════════════════
 * §1  Page Flags & Physical Page Descriptor (vm_page_t)
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef u32 vm_page_flags_t;

#define VM_PAGE_WIRED       (1u << 0)   /* Cannot be paged out              */
#define VM_PAGE_DIRTY       (1u << 1)   /* Modified, needs writeback        */
#define VM_PAGE_REFERENCED  (1u << 2)   /* Referenced recently (LRU clock)  */
#define VM_PAGE_BUSY        (1u << 3)   /* I/O in progress                  */
#define VM_PAGE_ERROR       (1u << 4)   /* I/O error on this page           */
#define VM_PAGE_PMAPPED     (1u << 5)   /* Mapped into at least one pmap    */
#define VM_PAGE_FICTITIOUS  (1u << 6)   /* Does not represent real RAM      */
#define VM_PAGE_ZERO        (1u << 7)   /* Zeroed and clean                 */
#define VM_PAGE_ENCRYPTED   (1u << 8)   /* Content is encrypted (swap)      */

typedef struct vm_page {
    // physical identity
    xiu_paddr_t         vmp_phys_addr;  // physical address of this page
    vm_page_flags_t     vmp_flags;      // vm_page_* flags

    // object linkage
    struct vm_object   *vmp_object;     // owning vm_object
    xiu_offset_t        vmp_offset;     // byte offset within object

    // lru / free list linkage
    struct vm_page     *vmp_next;       // next in object page list
    struct vm_page     *vmp_prev;       // prev in object page list
    struct vm_page     *vmp_lru_next;   // global LRU list
    struct vm_page     *vmp_lru_prev;

    // wait queue for busy pages
    struct xiu_thread  *vmp_waiter;

    // wire count
    u32                 vmp_wire_count;

    u32                 _pad;
} vm_page_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * §2  VM Object Protection & Inheritance
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef u32 vm_prot_t;

#define VM_PROT_NONE        ((vm_prot_t)0x00)
#define VM_PROT_READ        ((vm_prot_t)0x01)
#define VM_PROT_WRITE       ((vm_prot_t)0x02)
#define VM_PROT_EXECUTE     ((vm_prot_t)0x04)
#define VM_PROT_RW          (VM_PROT_READ | VM_PROT_WRITE)
#define VM_PROT_RX          (VM_PROT_READ | VM_PROT_EXECUTE)
#define VM_PROT_RWX         (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)
#define VM_PROT_ALL         VM_PROT_RWX

typedef u32 vm_inherit_t;

#define VM_INHERIT_SHARE    ((vm_inherit_t)0)   /* Shared across fork()     */
#define VM_INHERIT_COPY     ((vm_inherit_t)1)   /* COW copy on fork()       */
#define VM_INHERIT_NONE     ((vm_inherit_t)2)   /* Unmapped in child        */
#define VM_INHERIT_DEFAULT  VM_INHERIT_COPY

/* ═══════════════════════════════════════════════════════════════════════════
 * §3  Pager Type
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef u8 vm_pager_type_t;

#define VM_PAGER_NONE       ((vm_pager_type_t)0)  /* No pager (pre-allocated) */
#define VM_PAGER_ANON       ((vm_pager_type_t)1)  /* Anonymous / swap         */
#define VM_PAGER_VNODE      ((vm_pager_type_t)2)  /* File-backed (mmap)       */
#define VM_PAGER_DEVICE     ((vm_pager_type_t)3)  /* MMIO / device memory     */
#define VM_PAGER_PHYS       ((vm_pager_type_t)4)  /* Locked physical pages    */

/* ═══════════════════════════════════════════════════════════════════════════
 * §4  VM Object Flags
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef u32 vm_obj_flags_t;

#define VM_OBJ_INTERNAL     (1u << 0)  /* Anonymous (not file-backed)       */
#define VM_OBJ_COPY_ON_WRITE (1u << 1) /* Shadow: copy pages on first write */
#define VM_OBJ_SHADOW       (1u << 2)  /* This is a shadow object           */
#define VM_OBJ_PAGED_OUT    (1u << 3)  /* Some pages on swap                */
#define VM_OBJ_ALIVE        (1u << 4)  /* Object is live (not deallocated)  */
#define VM_OBJ_WIRED        (1u << 5)  /* All pages wired                   */
#define VM_OBJ_VNODE        (1u << 6)  /* Backed by a vnode                 */
#define VM_OBJ_DEVICE       (1u << 7)  /* MMIO region                       */
#define VM_OBJ_EXECUTABLE   (1u << 8)  /* Contains executable code          */
#define VM_OBJ_ENCRYPTED    (1u << 9)  /* Encrypted content (FairPlay)      */

/* ═══════════════════════════════════════════════════════════════════════════
 * §5  VM Object (vm_object_t)
 *
 * 128-byte structure aligned to cache line.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct XIU_ALIGNED(64) vm_object {
    u64                 vmo_signature;  // xiu_vm_object_magic
    spinlock_t          vmo_lock;       // protects pages + shadow chain

    // reference counting
    atomic_uint         vmo_ref_count;  // number of map entries referencing
    atomic_uint         vmo_res_count;  // resident page count

    // size
    xiu_size_t          vmo_size;       // object size in bytes

    // page list
    vm_page_t          *vmo_pages;      // sorted linked list of resident pg
    u32                 vmo_page_count; // number of resident pages

    // shadow chain
    struct vm_object   *vmo_shadow;
    xiu_offset_t        vmo_shadow_offset;
    u32                 vmo_shadow_count;

    // pager
    vm_pager_type_t     vmo_pager_type;
    u8                  _pad0[3];

    union {
        struct vnode   *vmo_vnode;
        xiu_paddr_t     vmo_phys_base;
    };
    xiu_offset_t        vmo_pager_offset;

    // protection / inheritance
    vm_prot_t           vmo_max_prot;
    vm_inherit_t        vmo_inherit;
    vm_obj_flags_t      vmo_flags;

    // statistics
    u64                 vmo_faults;
    u64                 vmo_pageouts;

    u8                  _pad1[8];
} vm_object_t;

#define XIU_VM_OBJECT_MAGIC  UINT64_C(0x5849554F424A4543)

XIU_WARN_UNUSED
xiu_error_t vm_object_create(xiu_size_t size,
                              vm_prot_t max_prot,
                              vm_object_t **obj_out);

XIU_WARN_UNUSED
xiu_error_t vm_object_create_vnode(struct vnode *vp,
                                    xiu_offset_t offset,
                                    xiu_size_t size,
                                    vm_object_t **obj_out);

XIU_WARN_UNUSED
xiu_error_t vm_object_shadow(vm_object_t *original,
                              xiu_offset_t offset,
                              xiu_size_t size,
                              vm_object_t **shadow_out);

void vm_object_reference(vm_object_t *obj);
void vm_object_deallocate(vm_object_t *obj);

XIU_WARN_UNUSED
xiu_error_t vm_object_fault(vm_object_t *obj,
                             xiu_offset_t offset,
                             vm_prot_t fault_type,
                             vm_page_t **page_out);

vm_page_t *vm_object_lookup_page(vm_object_t *obj, xiu_offset_t offset);

vm_page_t *vm_object_resolve_shadow(vm_object_t *obj, xiu_offset_t offset);

xiu_error_t vm_object_sync(vm_object_t *obj);

#endif /* XIU_VM_OBJECT_H */
