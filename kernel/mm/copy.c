/* =============================================================================
 * Chimera Operating System — Secure User/Kernel Memory Copy
 * kernel/mm/copy.c
 *
 * Implements strict boundary checking, page validation, and fault handling
 * for data transfers between Ring 3 (User) and Ring 0 (Kernel).
 *
 * Never fabricates zeroed pages on copyin reads; returns CHIMERA_ERR_INVALID (-EFAULT).
 * ============================================================================= */

#include <kernel/chimera_types.h>
#include <kernel/proc.h>

#define USER_SPACE_MIN      0x0000000000001000ULL
#define USER_SPACE_MAX      0x00007FFFFFFFFFFFULL
#define USER_STACK_MIN      0x00007FF000000000ULL
#define USER_STACK_MAX      0x00007FFFFFFFFFFFULL

extern u64 pmap_extract(u64 pml4_phys, u64 vaddr);
extern u64 pmap_map_user_page(u64 target_pml4_phys, u64 vaddr, u64 paddr, u32 flags);
extern chimera_paddr_t pmm_alloc_page(void);
extern void pmm_release_page(chimera_paddr_t paddr);

static inline u64 get_active_pml4(void) {
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & ~0xFFFULL;
}

/* ── copyin ──────────────────────────────────────────────────────────────── *
 * Copies data from user space to kernel space.
 * Fails with CHIMERA_ERR_INVALID (-EFAULT) if any page in the range is unmapped.
 * ────────────────────────────────────────────────────────────────────────── */
chimera_error_t copyin(const void *uaddr, void *kaddr, usize len) {
    if (!uaddr || !kaddr || len == 0) return CHIMERA_ERR_INVALID;
    if ((uptr)uaddr < USER_SPACE_MIN || len > (USER_SPACE_MAX - USER_SPACE_MIN) || (uptr)uaddr > USER_SPACE_MAX - len)
        return CHIMERA_ERR_INVALID;

    u64 pml4_phys = get_active_pml4();
    uptr curr_uaddr = (uptr)uaddr;
    uptr curr_kaddr = (uptr)kaddr;
    usize remaining = len;

    while (remaining > 0) {
        u64 phys = pmap_extract(pml4_phys, curr_uaddr);
        if (!phys) {
            // unmapped page: fail immediately with EFAULT, do not fabricate pages
            return CHIMERA_ERR_INVALID;
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

    return CHIMERA_SUCCESS;
}

/* ── copyout ─────────────────────────────────────────────────────────────── *
 * Copies data from kernel space to user space.
 * Only allows demand-paging in the valid user stack region.
 * ────────────────────────────────────────────────────────────────────────── */
chimera_error_t copyout(const void *kaddr, void *uaddr, usize len) {
    if (!uaddr || !kaddr || len == 0) return CHIMERA_ERR_INVALID;
    if ((uptr)uaddr < USER_SPACE_MIN || len > (USER_SPACE_MAX - USER_SPACE_MIN) || (uptr)uaddr > USER_SPACE_MAX - len)
        return CHIMERA_ERR_INVALID;

    u64 pml4_phys = get_active_pml4();
    uptr curr_uaddr = (uptr)uaddr;
    uptr curr_kaddr = (uptr)kaddr;
    usize remaining = len;

    while (remaining > 0) {
        u64 phys = pmap_extract(pml4_phys, curr_uaddr);
        if (!phys) {
            u64 page_vaddr = curr_uaddr & ~0xFFFULL;
            if (page_vaddr >= USER_STACK_MIN && page_vaddr <= USER_STACK_MAX) {
                u64 new_paddr = pmm_alloc_page();
                if (new_paddr == 0 || new_paddr == (chimera_paddr_t)-1)
                    return CHIMERA_ERR_INVALID;
                void *hhdm = (void *)(new_paddr + HHDM_BASE);
                __builtin_memset(hhdm, 0, 4096);
                if (pmap_map_user_page(pml4_phys, page_vaddr, new_paddr, 0x01 | 0x02 | 0x04) == 0) {
                    pmm_release_page(new_paddr);
                    return CHIMERA_ERR_INVALID;
                }
                phys = new_paddr | (curr_uaddr & 0xFFF);
            } else {
                return CHIMERA_ERR_INVALID;
            }
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

    return CHIMERA_SUCCESS;
}

/* ── copyinstr ───────────────────────────────────────────────────────────── *
 * Safely copies a null-terminated string from user space to kernel space.
 * Returns CHIMERA_SUCCESS on success, CHIMERA_ERR_INVALID on EFAULT, or CHIMERA_ERR_RANGE on overflow.
 * ────────────────────────────────────────────────────────────────────────── */
chimera_error_t copyinstr(const void *uaddr, char *kaddr, usize maxlen, usize *lencopied) {
    if (!uaddr || !kaddr || maxlen == 0) return CHIMERA_ERR_INVALID;
    if ((uptr)uaddr < USER_SPACE_MIN || (uptr)uaddr >= USER_SPACE_MAX) return CHIMERA_ERR_INVALID;

    u64 pml4_phys = get_active_pml4();
    uptr curr_uaddr = (uptr)uaddr;
    usize copied = 0;

    while (copied < maxlen) {
        if (curr_uaddr >= USER_SPACE_MAX) return CHIMERA_ERR_INVALID;

        u64 phys = pmap_extract(pml4_phys, curr_uaddr);
        if (!phys) {
            return CHIMERA_ERR_INVALID;
        }

        uptr page_offset = curr_uaddr & 0xFFF;
        usize page_remain = 4096 - page_offset;
        const char *src = (const char *)((phys & ~0xFFFULL) + HHDM_BASE + page_offset);

        while (page_remain > 0 && copied < maxlen) {
            char c = *src++;
            kaddr[copied++] = c;
            curr_uaddr++;
            page_remain--;

            if (c == '\0') {
                if (lencopied) *lencopied = copied;
                return CHIMERA_SUCCESS;
            }
        }
    }

    if (lencopied) *lencopied = copied;
    return CHIMERA_ERR_RANGE; // string exceeded maxlen without null terminator
}
