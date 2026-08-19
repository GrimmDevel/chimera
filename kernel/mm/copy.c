/* =============================================================================
 * XIU Operating System — Secure User/Kernel Memory Copy
 * kernel/mm/copy.c
 * ============================================================================= */

#include <kernel/xiu_types.h>
#include <kernel/proc.h>

#define USER_SPACE_MIN 0x0000000000001000ULL
#define USER_SPACE_MAX 0x00007FFFFFFFFFFFULL

extern u64 pmap_extract(u64 pml4_phys, u64 vaddr);
extern u64 pmap_map_user_page(u64 target_pml4_phys, u64 vaddr, u64 paddr, u32 flags);
extern xiu_paddr_t pmm_alloc_page(void);
extern xiu_thread_t *current_thread(void);

static inline u64 get_active_pml4(void) {
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & ~0xFFFULL;
}

/* ── copyin ──────────────────────────────────────────────────────────────── *
 * Copies data from user space to kernel space.
 * Returns XIU_SUCCESS (0) on success, or XIU_ERR_INVALID (-1) on failure.
 * ────────────────────────────────────────────────────────────────────────── */
xiu_error_t copyin(const void *uaddr, void *kaddr, usize len) {
    if (!uaddr || !kaddr || len == 0) return XIU_ERR_INVALID;
    if ((uptr)uaddr < USER_SPACE_MIN || (uptr)uaddr + len > USER_SPACE_MAX) return XIU_ERR_INVALID;

    u64 pml4_phys = get_active_pml4();
    uptr curr_uaddr = (uptr)uaddr;
    uptr curr_kaddr = (uptr)kaddr;
    usize remaining = len;

    while (remaining > 0) {
        u64 phys = pmap_extract(pml4_phys, curr_uaddr);
        if (!phys) {
            u64 page_vaddr = curr_uaddr & ~0xFFFULL;
            if (page_vaddr < USER_SPACE_MIN) return XIU_ERR_INVALID;
            u64 new_paddr = pmm_alloc_page();
            if (new_paddr == 0 || new_paddr == (xiu_paddr_t)-1)
                return XIU_ERR_INVALID;
            void *hhdm = (void *)(new_paddr + HHDM_BASE);
            __builtin_memset(hhdm, 0, 4096);
            pmap_map_user_page(pml4_phys, page_vaddr, new_paddr, 0x01 | 0x02 | 0x04);
            phys = new_paddr | (curr_uaddr & 0xFFF);
        }

        uptr page_offset = curr_uaddr & 0xFFF;
        usize chunk = 4096 - page_offset;
        if (chunk > remaining) chunk = remaining;

        void *hhdm_ptr = (void *)((phys & ~0xFFFULL) + HHDM_BASE + page_offset);
        __builtin_memcpy((void *)curr_kaddr, hhdm_ptr, chunk);

        curr_uaddr += chunk;
        curr_kaddr += chunk;
        remaining -= chunk;
    }

    return XIU_SUCCESS;
}

/* ── copyout ─────────────────────────────────────────────────────────────── *
 * Copies data from kernel space to user space.
 * Returns XIU_SUCCESS (0) on success, or XIU_ERR_INVALID (-1) on failure.
 * ────────────────────────────────────────────────────────────────────────── */
xiu_error_t copyout(const void *kaddr, void *uaddr, usize len) {
    if (!uaddr || !kaddr || len == 0) return XIU_ERR_INVALID;
    if ((uptr)uaddr < USER_SPACE_MIN || (uptr)uaddr + len > USER_SPACE_MAX) return XIU_ERR_INVALID;

    u64 pml4_phys = get_active_pml4();
    uptr curr_uaddr = (uptr)uaddr;
    uptr curr_kaddr = (uptr)kaddr;
    usize remaining = len;

    while (remaining > 0) {
        u64 phys = pmap_extract(pml4_phys, curr_uaddr);
        if (!phys) {
            u64 page_vaddr = curr_uaddr & ~0xFFFULL;
            if (page_vaddr < USER_SPACE_MIN) return XIU_ERR_INVALID;
            u64 new_paddr = pmm_alloc_page();
            if (new_paddr == 0 || new_paddr == (xiu_paddr_t)-1)
                return XIU_ERR_INVALID;
            void *hhdm = (void *)(new_paddr + HHDM_BASE);
            __builtin_memset(hhdm, 0, 4096);
            pmap_map_user_page(pml4_phys, page_vaddr, new_paddr, 0x01 | 0x02 | 0x04);
            phys = new_paddr | (curr_uaddr & 0xFFF);
        }

        uptr page_offset = curr_uaddr & 0xFFF;
        usize chunk = 4096 - page_offset;
        if (chunk > remaining) chunk = remaining;

        void *hhdm_ptr = (void *)((phys & ~0xFFFULL) + HHDM_BASE + page_offset);
        __builtin_memcpy(hhdm_ptr, (const void *)curr_kaddr, chunk);

        curr_uaddr += chunk;
        curr_kaddr += chunk;
        remaining -= chunk;
    }

    return XIU_SUCCESS;
}
