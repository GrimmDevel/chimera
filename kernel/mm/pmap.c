/* =============================================================================
 * XIU Operating System — Physical/Virtual Map (pmap)
 * kernel/mm/pmap.c
 * ============================================================================= */

#include <kernel/xiu_types.h>

extern xiu_paddr_t pmm_alloc_page(void);
extern void kprintf(const char *fmt, ...);

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITE   (1ULL << 1)
#define PAGE_USER    (1ULL << 2)
#define PAGE_COW     (1ULL << 9)  /* Available for OS: Copy-on-Write bit */

extern void pmm_retain_page(xiu_paddr_t addr);
extern void pmm_release_page(xiu_paddr_t addr);
#include <kernel/spinlock.h>

static u64 s_kernel_pml4_phys = 0;
static spinlock_t s_pmap_lock = SPINLOCK_INIT;

void pmap_bootstrap(void) {
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    s_kernel_pml4_phys = cr3 & ~0xFFFULL;
    spinlock_init(&s_pmap_lock);
    kprintf("        pmap: master kernel PML4 at phys=0x%llx, HHDM at virt=0x%llx\n",
            (unsigned long long)s_kernel_pml4_phys, (unsigned long long)g_hhdm_base);
}

u64 pmap_kernel_pml4(void) {
    return s_kernel_pml4_phys;
}

static inline u64 *get_table_ptr(u64 phys) {
    return (u64 *)(phys + g_hhdm_base);
}

u64 *pmap_get_pte_ptr(u64 pml4_phys, u64 vaddr) {
    if (!pml4_phys) return nullptr;
    u64 *pml4 = get_table_ptr(pml4_phys & ~0xFFFULL);
    u64 pml4e = pml4[(vaddr >> 39) & 0x1FF];
    if (!(pml4e & PAGE_PRESENT)) return nullptr;

    u64 *pdpt = get_table_ptr(pml4e & ~0xFFFULL);
    u64 pdpte = pdpt[(vaddr >> 30) & 0x1FF];
    if (!(pdpte & PAGE_PRESENT) || (pdpte & (1ULL << 7))) return nullptr;

    u64 *pd = get_table_ptr(pdpte & ~0xFFFULL);
    u64 pde = pd[(vaddr >> 21) & 0x1FF];
    if (!(pde & PAGE_PRESENT) || (pde & (1ULL << 7))) return nullptr;

    u64 *pt = get_table_ptr(pde & ~0xFFFULL);
    return &pt[(vaddr >> 12) & 0x1FF];
}

u64 pmap_map_user_page(u64 target_pml4_phys, u64 vaddr, u64 paddr, u32 flags) {
    u64 *pml4 = get_table_ptr(target_pml4_phys & ~0xFFFULL);

    int pml4_idx = (vaddr >> 39) & 0x1FF;
    int pdpt_idx = (vaddr >> 30) & 0x1FF;
    int pd_idx   = (vaddr >> 21) & 0x1FF;
    int pt_idx   = (vaddr >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        u64 phys = pmm_alloc_page();
        __builtin_memset(get_table_ptr(phys), 0, 4096);
        pml4[pml4_idx] = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    } else {
        pml4[pml4_idx] |= PAGE_USER | PAGE_WRITE;
    }

    u64 *pdpt = get_table_ptr(pml4[pml4_idx] & ~0xFFFULL);
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        u64 phys = pmm_alloc_page();
        __builtin_memset(get_table_ptr(phys), 0, 4096);
        pdpt[pdpt_idx] = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    } else {
        pdpt[pdpt_idx] |= PAGE_USER | PAGE_WRITE;
    }

    u64 *pd = get_table_ptr(pdpt[pdpt_idx] & ~0xFFFULL);
    if (!(pd[pd_idx] & PAGE_PRESENT) || (pd[pd_idx] & (1ULL << 7))) {
        u64 phys = pmm_alloc_page();
        __builtin_memset(get_table_ptr(phys), 0, 4096);
        pd[pd_idx] = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    } else {
        pd[pd_idx] |= PAGE_USER | PAGE_WRITE;
    }

    u64 *pt = get_table_ptr(pd[pd_idx] & ~0xFFFULL);
    pt[pt_idx] = (paddr & ~0xFFFULL) | flags | PAGE_PRESENT | PAGE_USER;
    return paddr;
}

// pmap_extract
u64 pmap_extract(u64 pml4_phys, u64 vaddr) {
    if (!pml4_phys) return 0;
    
    u64 *pml4 = get_table_ptr(pml4_phys & ~0xFFFULL);
    u64 pml4e = pml4[(vaddr >> 39) & 0x1FF];
    if (!(pml4e & PAGE_PRESENT) || !(pml4e & PAGE_USER)) return 0;

    u64 *pdpt = get_table_ptr(pml4e & ~0xFFFULL);
    u64 pdpte = pdpt[(vaddr >> 30) & 0x1FF];
    if (!(pdpte & PAGE_PRESENT) || !(pdpte & PAGE_USER)) return 0;
    
    if (pdpte & (1ULL << 7)) {
        return (pdpte & 0x000FFFFFC0000000ULL) | (vaddr & 0x3FFFFFFFULL);
    }

    u64 *pd = get_table_ptr(pdpte & ~0xFFFULL);
    u64 pde = pd[(vaddr >> 21) & 0x1FF];
    if (!(pde & PAGE_PRESENT) || !(pde & PAGE_USER)) return 0;
    
    if (pde & (1ULL << 7)) {
        return (pde & 0x000FFFFFFFE00000ULL) | (vaddr & 0x1FFFFFULL);
    }

    u64 *pt = get_table_ptr(pde & ~0xFFFULL);
    u64 pte = pt[(vaddr >> 12) & 0x1FF];
    if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER)) return 0;

    return (pte & 0x000FFFFFFFFFF000ULL) | (vaddr & 0xFFFULL);
}

/* ── Copy-on-Write (COW) Fork Cloning ────────────────────────────────────── *
 * Duplicates page tables with zero-copy COW mappings.
 * Pages marked writable are downgraded to Read-Only + PAGE_COW in both parent
 * and child, and physical page refcounts are incremented.
 * ─────────────────────────────────────────────────────────────────────────── */
u64 pmap_clone_user_space(u64 src_pml4_phys) {
    u64 dst_pml4_phys = pmm_alloc_page();
    u64 *dst_pml4 = get_table_ptr(dst_pml4_phys);
    u64 *src_pml4 = get_table_ptr(src_pml4_phys & ~0xFFFULL);
    __builtin_memset(dst_pml4, 0, 4096);

    // copy kernel address space
    for (int i = 256; i < 512; i++) {
        dst_pml4[i] = src_pml4[i];
    }

    // copy user address space
    for (u64 pml4_i = 0; pml4_i < 256; pml4_i++) {
        u64 pml4e = src_pml4[pml4_i];
        if (!(pml4e & PAGE_PRESENT) || !(pml4e & PAGE_USER)) continue;

        u64 *src_pdpt = get_table_ptr(pml4e & ~0xFFFULL);
        for (u64 pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            u64 pdpte = src_pdpt[pdpt_i];
            if (!(pdpte & PAGE_PRESENT) || !(pdpte & PAGE_USER) || (pdpte & (1ULL << 7)))
                continue;

            u64 *src_pd = get_table_ptr(pdpte & ~0xFFFULL);
            for (u64 pd_i = 0; pd_i < 512; pd_i++) {
                u64 pde = src_pd[pd_i];
                if (!(pde & PAGE_PRESENT) || !(pde & PAGE_USER) || (pde & (1ULL << 7)))
                    continue;

                u64 *src_pt = get_table_ptr(pde & ~0xFFFULL);
                for (u64 pt_i = 0; pt_i < 512; pt_i++) {
                    u64 pte = src_pt[pt_i];
                    if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER)) continue;

                    u64 va = (pml4_i << 39) | (pdpt_i << 30) | (pd_i << 21) | (pt_i << 12);
                    u64 phys = pte & ~0xFFFULL;

                    u32 flags = 0;
                    if (pte & PAGE_WRITE) {
                        // downgrade parent PTE to Read-Only + COW
                        src_pt[pt_i] = (src_pt[pt_i] & ~PAGE_WRITE) | PAGE_COW;
                        flags = PAGE_COW;
                    } else if (pte & PAGE_COW) {
                        flags = PAGE_COW;
                    }

                    // map shared page into child with COW
                    pmap_map_user_page(dst_pml4_phys, va, phys, flags);
                    pmm_retain_page(phys);
                }
            }
        }
    }

    u64 cr3_val;
    __asm__ volatile("mov %%cr3, %0; mov %0, %%cr3" : "=r"(cr3_val));

    return dst_pml4_phys;
}

/* ── Full recursive destruction of a user address space ─────────────────── *
 * Frees all user physical pages and all intermediate page table pages.
 * ─────────────────────────────────────────────────────────────────────────── */
void pmap_destroy_user_space(u64 pml4_phys) {
    if (!pml4_phys) return;
    u64 *pml4 = get_table_ptr(pml4_phys & ~0xFFFULL);

    for (u64 pml4_i = 0; pml4_i < 256; pml4_i++) {
        u64 pml4e = pml4[pml4_i];
        if (!(pml4e & PAGE_PRESENT) || !(pml4e & PAGE_USER)) continue;

        u64 *pdpt = get_table_ptr(pml4e & ~0xFFFULL);
        for (u64 pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            u64 pdpte = pdpt[pdpt_i];
            if (!(pdpte & PAGE_PRESENT) || !(pdpte & PAGE_USER) || (pdpte & (1ULL << 7)))
                continue;

            u64 *pd = get_table_ptr(pdpte & ~0xFFFULL);
            for (u64 pd_i = 0; pd_i < 512; pd_i++) {
                u64 pde = pd[pd_i];
                if (!(pde & PAGE_PRESENT) || !(pde & PAGE_USER) || (pde & (1ULL << 7)))
                    continue;

                u64 *pt = get_table_ptr(pde & ~0xFFFULL);
                for (u64 pt_i = 0; pt_i < 512; pt_i++) {
                    u64 pte = pt[pt_i];
                    if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER)) continue;

                    u64 phys = pte & ~0xFFFULL;
                    pt[pt_i] = 0;
                    pmm_release_page(phys);
                }

                // free PT page
                pmm_release_page(pde & ~0xFFFULL);
                pd[pd_i] = 0;
            }

            // free PD page
            pmm_release_page(pdpte & ~0xFFFULL);
            pdpt[pdpt_i] = 0;
        }

        // free PDPT page
        pmm_release_page(pml4e & ~0xFFFULL);
        pml4[pml4_i] = 0;
    }

    // free PML4 itself
    pmm_release_page(pml4_phys & ~0xFFFULL);
}

void pmap_unmap_user_range(u64 pml4_phys, u64 vaddr, usize len) {
    if (!pml4_phys || len == 0) return;
    u64 start = vaddr & ~0xFFFULL;
    u64 end = (vaddr + len + 4095) & ~0xFFFULL;

    for (u64 va = start; va < end; va += 4096) {
        u64 *pte_ptr = pmap_get_pte_ptr(pml4_phys, va);
        if (pte_ptr && (*pte_ptr & PAGE_PRESENT)) {
            u64 phys = *pte_ptr & ~0xFFFULL;
            *pte_ptr = 0;
            pmm_release_page(phys);
            __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
        }
    }
}

void pmap_clear_user_mappings(u64 pml4_phys) {
    if (!pml4_phys) return;
    u64 *pml4 = get_table_ptr(pml4_phys & ~0xFFFULL);

    // free user pages and page tables
    for (u64 pml4_i = 0; pml4_i < 256; pml4_i++) {
        u64 pml4e = pml4[pml4_i];
        if (!(pml4e & PAGE_PRESENT) || !(pml4e & PAGE_USER)) continue;

        u64 *pdpt = get_table_ptr(pml4e & ~0xFFFULL);
        for (u64 pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            u64 pdpte = pdpt[pdpt_i];
            if (!(pdpte & PAGE_PRESENT) || !(pdpte & PAGE_USER) || (pdpte & (1ULL << 7)))
                continue;

            u64 *pd = get_table_ptr(pdpte & ~0xFFFULL);
            for (u64 pd_i = 0; pd_i < 512; pd_i++) {
                u64 pde = pd[pd_i];
                if (!(pde & PAGE_PRESENT) || !(pde & PAGE_USER) || (pde & (1ULL << 7)))
                    continue;

                u64 *pt = get_table_ptr(pde & ~0xFFFULL);
                for (u64 pt_i = 0; pt_i < 512; pt_i++) {
                    u64 pte = pt[pt_i];
                    if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER)) continue;

                    u64 phys = pte & ~0xFFFULL;
                    pt[pt_i] = 0;
                    pmm_release_page(phys);
                }

                pmm_release_page(pde & ~0xFFFULL);
                pd[pd_i] = 0;
            }

            pmm_release_page(pdpte & ~0xFFFULL);
            pdpt[pdpt_i] = 0;
        }

        pmm_release_page(pml4e & ~0xFFFULL);
        pml4[pml4_i] = 0;
    }

    // flush TLB
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0; mov %0, %%cr3" : "=r"(cr3));
}
