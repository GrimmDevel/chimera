/* =============================================================================
 * XIU Operating System — Secure User/Kernel Memory Copy
 * kernel/mm/copy.c
 *
 * Implements strict boundary checking, page validation, and fault handling
 * for data transfers between Ring 3 (User) and Ring 0 (Kernel).
 *
 * Never fabricates zeroed pages on copyin reads; returns XIU_ERR_INVALID (-EFAULT).
 * ============================================================================= */

#include <kernel/xiu_types.h>
#include <kernel/proc.h>

#define USER_SPACE_MIN      0x0000000000001000ULL
#define USER_SPACE_MAX      0x00007FFFFFFFFFFFULL
#define USER_STACK_MIN      0x00007FF000000000ULL
#define USER_STACK_MAX      0x00007FFFFFFFFFFFULL

extern u64 pmap_extract(u64 pml4_phys, u64 vaddr);
extern u64 pmap_map_user_page(u64 target_pml4_phys, u64 vaddr, u64 paddr, u32 flags);
extern xiu_paddr_t pmm_alloc_page(void);

static inline u64 get_active_pml4(void) {
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & ~0xFFFULL;
}

/* ── copyin ──────────────────────────────────────────────────────────────── *
 * Copies data from user space to kernel space.
 * Fails with XIU_ERR_INVALID (-EFAULT) if any page in the range is unmapped.
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
            // unmapped page: fail immediately with EFAULT, do not fabricate pages
            return XIU_ERR_INVALID;
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
 * Only allows demand-paging in the valid user stack region.
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
            if (page_vaddr >= USER_STACK_MIN && page_vaddr <= USER_STACK_MAX) {
                u64 new_paddr = pmm_alloc_page();
                if (new_paddr == 0 || new_paddr == (xiu_paddr_t)-1)
                    return XIU_ERR_INVALID;
                void *hhdm = (void *)(new_paddr + HHDM_BASE);
                __builtin_memset(hhdm, 0, 4096);
                pmap_map_user_page(pml4_phys, page_vaddr, new_paddr, 0x01 | 0x02 | 0x04);
                phys = new_paddr | (curr_uaddr & 0xFFF);
            } else {
                return XIU_ERR_INVALID;
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

    return XIU_SUCCESS;
}

/* ── copyinstr ───────────────────────────────────────────────────────────── *
 * Safely copies a null-terminated string from user space to kernel space.
 * Returns XIU_SUCCESS on success, XIU_ERR_INVALID on EFAULT, or XIU_ERR_RANGE on overflow.
 * ────────────────────────────────────────────────────────────────────────── */
xiu_error_t copyinstr(const void *uaddr, char *kaddr, usize maxlen, usize *lencopied) {
    if (!uaddr || !kaddr || maxlen == 0) return XIU_ERR_INVALID;
    if ((uptr)uaddr < USER_SPACE_MIN || (uptr)uaddr >= USER_SPACE_MAX) return XIU_ERR_INVALID;

    u64 pml4_phys = get_active_pml4();
    uptr curr_uaddr = (uptr)uaddr;
    usize copied = 0;

    while (copied < maxlen) {
        u64 phys = pmap_extract(pml4_phys, curr_uaddr);
        if (!phys) {
            return XIU_ERR_INVALID;
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
                return XIU_SUCCESS;
            }
        }
    }

    if (lencopied) *lencopied = copied;
    return XIU_ERR_RANGE; // string exceeded maxlen without null terminator
}
