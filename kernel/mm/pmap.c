/* =============================================================================
 * Chimera Operating System — Physical/Virtual Map (pmap)
 * kernel/mm/pmap.c
 * ============================================================================= */

#include <kernel/chimera_types.h>
#include <kernel/spinlock.h>
#include <kernel/smp.h>

extern chimera_paddr_t pmm_alloc_page(void);
extern void kprintf(const char *fmt, ...);

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITE   (1ULL << 1)
#define PAGE_USER    (1ULL << 2)
#define PAGE_COW     (1ULL << 9)  /* Available for OS: Copy-on-Write bit */
#define PTE_PHYS_MASK 0x000FFFFFFFFFF000ULL

extern void pmm_retain_page(chimera_paddr_t addr);
extern void pmm_release_page(chimera_paddr_t addr);

static u64 s_kernel_pml4_phys = 0;
static spinlock_t s_pmap_lock = SPINLOCK_INIT;

void pmap_bootstrap(void) {
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    s_kernel_pml4_phys = cr3 & PTE_PHYS_MASK;
    spinlock_init(&s_pmap_lock);
    kprintf("        pmap: master kernel PML4 at phys=0x%llx, HHDM at virt=0x%llx\n",
            (unsigned long long)s_kernel_pml4_phys, (unsigned long long)g_hhdm_base);
}

u64 pmap_kernel_pml4(void) {
    return s_kernel_pml4_phys;
}

static inline u64 *get_table_ptr(u64 phys) {
    return (u64 *)((phys & PTE_PHYS_MASK) + g_hhdm_base);
}

u64 pmap_create(void) {
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);
    u64 pml4_phys = pmm_alloc_page();
    if (pml4_phys == 0 || pml4_phys == (u64)-1) {
        spinlock_unlock_irqrestore(&s_pmap_lock, irq);
        return 0;
    }

    u64 *pml4 = get_table_ptr(pml4_phys);
    u64 *k_pml4 = get_table_ptr(s_kernel_pml4_phys);
    __builtin_memset(pml4, 0, 4096);

    // copy kernel higher-half mappings (entries 256..511)
    for (int i = 256; i < 512; i++) {
        pml4[i] = k_pml4[i];
    }

    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
    return pml4_phys;
}

u64 *pmap_get_pte_ptr(u64 pml4_phys, u64 vaddr) {
    if (!pml4_phys) return nullptr;
    u64 *pml4 = get_table_ptr(pml4_phys);
    u64 pml4e = pml4[(vaddr >> 39) & 0x1FF];
    if (!(pml4e & PAGE_PRESENT)) return nullptr;

    u64 *pdpt = get_table_ptr(pml4e);
    u64 pdpte = pdpt[(vaddr >> 30) & 0x1FF];
    if (!(pdpte & PAGE_PRESENT) || (pdpte & (1ULL << 7))) return nullptr;

    u64 *pd = get_table_ptr(pdpte);
    u64 pde = pd[(vaddr >> 21) & 0x1FF];
    if (!(pde & PAGE_PRESENT) || (pde & (1ULL << 7))) return nullptr;

    u64 *pt = get_table_ptr(pde);
    return &pt[(vaddr >> 12) & 0x1FF];
}

static u64 pmap_map_user_page_unlocked(u64 target_pml4_phys, u64 vaddr, u64 paddr, u32 flags) {
    if (!target_pml4_phys || vaddr < 0x1000 || vaddr >= 0x0000800000000000ULL)
        return 0;

    int pml4_idx = (vaddr >> 39) & 0x1FF;
    if (pml4_idx >= 256) return 0; // strictly user address space

    int pdpt_idx = (vaddr >> 30) & 0x1FF;
    int pd_idx   = (vaddr >> 21) & 0x1FF;
    int pt_idx   = (vaddr >> 12) & 0x1FF;

    u64 *pml4 = get_table_ptr(target_pml4_phys);

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        u64 phys = pmm_alloc_page();
        if (phys == 0 || phys == (u64)-1) return 0;
        __builtin_memset(get_table_ptr(phys), 0, 4096);
        pml4[pml4_idx] = (phys & PTE_PHYS_MASK) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    } else {
        if (!(pml4[pml4_idx] & PAGE_USER)) return 0; // never escalate kernel entries
    }

    u64 *pdpt = get_table_ptr(pml4[pml4_idx]);
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        u64 phys = pmm_alloc_page();
        if (phys == 0 || phys == (u64)-1) return 0;
        __builtin_memset(get_table_ptr(phys), 0, 4096);
        pdpt[pdpt_idx] = (phys & PTE_PHYS_MASK) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    } else {
        if (!(pdpt[pdpt_idx] & PAGE_USER)) return 0;
    }

    u64 *pd = get_table_ptr(pdpt[pdpt_idx]);
    if (!(pd[pd_idx] & PAGE_PRESENT) || (pd[pd_idx] & (1ULL << 7))) {
        u64 phys = pmm_alloc_page();
        if (phys == 0 || phys == (u64)-1) return 0;
        __builtin_memset(get_table_ptr(phys), 0, 4096);
        pd[pd_idx] = (phys & PTE_PHYS_MASK) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    } else {
        if (!(pd[pd_idx] & PAGE_USER)) return 0;
    }

    u64 *pt = get_table_ptr(pd[pd_idx]);
    pt[pt_idx] = (paddr & PTE_PHYS_MASK) | flags | PAGE_PRESENT | PAGE_USER;
    return paddr;
}

u64 pmap_map_user_page(u64 target_pml4_phys, u64 vaddr, u64 paddr, u32 flags) {
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);
    u64 ret = pmap_map_user_page_unlocked(target_pml4_phys, vaddr, paddr, flags);
    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
    return ret;
}

// pmap_extract
u64 pmap_extract(u64 pml4_phys, u64 vaddr) {
    if (!pml4_phys || vaddr < 0x1000 || vaddr >= 0x0000800000000000ULL) return 0;
    
    u64 *pml4 = get_table_ptr(pml4_phys);
    u64 pml4e = pml4[(vaddr >> 39) & 0x1FF];
    if (!(pml4e & PAGE_PRESENT) || !(pml4e & PAGE_USER)) return 0;

    u64 *pdpt = get_table_ptr(pml4e);
    u64 pdpte = pdpt[(vaddr >> 30) & 0x1FF];
    if (!(pdpte & PAGE_PRESENT) || !(pdpte & PAGE_USER)) return 0;
    
    if (pdpte & (1ULL << 7)) {
        return (pdpte & 0x000FFFFFC0000000ULL) | (vaddr & 0x3FFFFFFFULL);
    }

    u64 *pd = get_table_ptr(pdpte);
    u64 pde = pd[(vaddr >> 21) & 0x1FF];
    if (!(pde & PAGE_PRESENT) || !(pde & PAGE_USER)) return 0;
    
    if (pde & (1ULL << 7)) {
        return (pde & 0x000FFFFFFFE00000ULL) | (vaddr & 0x1FFFFFULL);
    }

    u64 *pt = get_table_ptr(pde);
    u64 pte = pt[(vaddr >> 12) & 0x1FF];
    if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER)) return 0;

    return (pte & PTE_PHYS_MASK) | (vaddr & 0xFFFULL);
}

/* ── Copy-on-Write (COW) Fork Cloning ────────────────────────────────────── *
 * Duplicates page tables with zero-copy COW mappings.
 * Pages marked writable are downgraded to Read-Only + PAGE_COW in both parent
 * and child, and physical page refcounts are incremented.
 * ─────────────────────────────────────────────────────────────────────────── */
u64 pmap_clone_user_space(u64 src_pml4_phys) {
    if (!src_pml4_phys) return 0;
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);

    u64 dst_pml4_phys = pmm_alloc_page();
    if (dst_pml4_phys == 0 || dst_pml4_phys == (u64)-1) {
        spinlock_unlock_irqrestore(&s_pmap_lock, irq);
        return 0;
    }
    u64 *dst_pml4 = get_table_ptr(dst_pml4_phys);
    u64 *src_pml4 = get_table_ptr(src_pml4_phys);
    __builtin_memset(dst_pml4, 0, 4096);

    // copy kernel address space
    for (int i = 256; i < 512; i++) {
        dst_pml4[i] = src_pml4[i];
    }

    // copy user address space
    for (u64 pml4_i = 0; pml4_i < 256; pml4_i++) {
        u64 pml4e = src_pml4[pml4_i];
        if (!(pml4e & PAGE_PRESENT) || !(pml4e & PAGE_USER)) continue;

        u64 *src_pdpt = get_table_ptr(pml4e);
        for (u64 pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            u64 pdpte = src_pdpt[pdpt_i];
            if (!(pdpte & PAGE_PRESENT) || !(pdpte & PAGE_USER) || (pdpte & (1ULL << 7)))
                continue;

            u64 *src_pd = get_table_ptr(pdpte);
            for (u64 pd_i = 0; pd_i < 512; pd_i++) {
                u64 pde = src_pd[pd_i];
                if (!(pde & PAGE_PRESENT) || !(pde & PAGE_USER) || (pde & (1ULL << 7)))
                    continue;

                u64 *src_pt = get_table_ptr(pde);
                for (u64 pt_i = 0; pt_i < 512; pt_i++) {
                    u64 pte = src_pt[pt_i];
                    if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER)) continue;

                    u64 va = (pml4_i << 39) | (pdpt_i << 30) | (pd_i << 21) | (pt_i << 12);
                    u64 phys = pte & PTE_PHYS_MASK;

                    u32 flags = 0;
                    if (pte & PAGE_WRITE) {
                        // downgrade parent PTE to Read-Only + COW
                        src_pt[pt_i] = (src_pt[pt_i] & ~PAGE_WRITE) | PAGE_COW;
                        flags = PAGE_COW;
                    } else if (pte & PAGE_COW) {
                        flags = PAGE_COW;
                    }

                    // map shared page into child with COW
                    if (pmap_map_user_page_unlocked(dst_pml4_phys, va, phys, flags) != 0) {
                        pmm_retain_page(phys);
                    }
                }
            }
        }
    }

    smp_tlb_shootdown();

    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
    return dst_pml4_phys;
}

/* ── Full recursive destruction of a user address space ─────────────────── *
 * Frees all user physical pages and all intermediate page table pages.
 * ─────────────────────────────────────────────────────────────────────────── */
void pmap_destroy_user_space(u64 pml4_phys) {
    if (!pml4_phys) return;
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);

    // switch to master kernel PML4 if current CPU is executing on this map
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if ((cr3 & PTE_PHYS_MASK) == (pml4_phys & PTE_PHYS_MASK)) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(s_kernel_pml4_phys) : "memory");
    }

    u64 *pml4 = get_table_ptr(pml4_phys);

    for (u64 pml4_i = 0; pml4_i < 256; pml4_i++) {
        u64 pml4e = pml4[pml4_i];
        if (!(pml4e & PAGE_PRESENT)) continue;

        u64 *pdpt = get_table_ptr(pml4e);
        for (u64 pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            u64 pdpte = pdpt[pdpt_i];
            if (!(pdpte & PAGE_PRESENT) || (pdpte & (1ULL << 7)))
                continue;

            u64 *pd = get_table_ptr(pdpte);
            for (u64 pd_i = 0; pd_i < 512; pd_i++) {
                u64 pde = pd[pd_i];
                if (!(pde & PAGE_PRESENT) || (pde & (1ULL << 7)))
                    continue;

                u64 *pt = get_table_ptr(pde);
                for (u64 pt_i = 0; pt_i < 512; pt_i++) {
                    u64 pte = pt[pt_i];
                    if (!(pte & PAGE_PRESENT)) continue;

                    u64 phys = pte & PTE_PHYS_MASK;
                    pt[pt_i] = 0;
                    pmm_release_page(phys);
                }

                // free PT page
                pmm_release_page(pde & PTE_PHYS_MASK);
                pd[pd_i] = 0;
            }

            // free PD page
            pmm_release_page(pdpte & PTE_PHYS_MASK);
            pdpt[pdpt_i] = 0;
        }

        // free PDPT page
        pmm_release_page(pml4e & PTE_PHYS_MASK);
        pml4[pml4_i] = 0;
    }

    // free PML4 itself
    pmm_release_page(pml4_phys & PTE_PHYS_MASK);

    smp_tlb_shootdown();

    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
}

void pmap_unmap_user_range(u64 pml4_phys, u64 vaddr, usize len) {
    if (!pml4_phys || len == 0) return;
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);

    u64 start = vaddr & ~0xFFFULL;
    u64 end = (vaddr + len + 4095) & ~0xFFFULL;

    for (u64 va = start; va < end; va += 4096) {
        u64 *pte_ptr = pmap_get_pte_ptr(pml4_phys, va);
        if (pte_ptr && (*pte_ptr & PAGE_PRESENT)) {
            u64 phys = *pte_ptr & PTE_PHYS_MASK;
            *pte_ptr = 0;
            pmm_release_page(phys);
        }
    }

    smp_tlb_flush_range(start, end - start);

    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
}

void pmap_clear_user_mappings(u64 pml4_phys) {
    if (!pml4_phys) return;
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);

    u64 *pml4 = get_table_ptr(pml4_phys);

    // free user pages and page tables
    for (u64 pml4_i = 0; pml4_i < 256; pml4_i++) {
        u64 pml4e = pml4[pml4_i];
        if (!(pml4e & PAGE_PRESENT) || !(pml4e & PAGE_USER)) continue;

        u64 *pdpt = get_table_ptr(pml4e);
        for (u64 pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            u64 pdpte = pdpt[pdpt_i];
            if (!(pdpte & PAGE_PRESENT) || !(pdpte & PAGE_USER) || (pdpte & (1ULL << 7)))
                continue;

            u64 *pd = get_table_ptr(pdpte);
            for (u64 pd_i = 0; pd_i < 512; pd_i++) {
                u64 pde = pd[pd_i];
                if (!(pde & PAGE_PRESENT) || !(pde & PAGE_USER) || (pde & (1ULL << 7)))
                    continue;

                u64 *pt = get_table_ptr(pde);
                for (u64 pt_i = 0; pt_i < 512; pt_i++) {
                    u64 pte = pt[pt_i];
                    if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER)) continue;

                    u64 phys = pte & PTE_PHYS_MASK;
                    pt[pt_i] = 0;
                    pmm_release_page(phys);
                }

                pmm_release_page(pde & PTE_PHYS_MASK);
                pd[pd_i] = 0;
            }

            pmm_release_page(pdpte & PTE_PHYS_MASK);
            pdpt[pdpt_i] = 0;
        }

        pmm_release_page(pml4e & PTE_PHYS_MASK);
        pml4[pml4_i] = 0;
    }

    smp_tlb_shootdown();

    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
}

#define PAGE_NX (1ULL << 63)

int pmap_protect_user_range(u64 pml4_phys, u64 virt_start, usize len, u32 prot) {
    if (!pml4_phys || len == 0) return -1;
    u64 start_va = virt_start & ~0xFFFULL;
    u64 end_va = (virt_start + len + 0xFFFULL) & ~0xFFFULL;
    if (end_va >= 0x0000800000000000ULL) return -1;

    irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);

    for (u64 va = start_va; va < end_va; va += 4096) {
        u64 *pte_ptr = pmap_get_pte_ptr(pml4_phys, va);
        if (pte_ptr && (*pte_ptr & PAGE_PRESENT)) {
            u64 pte = *pte_ptr;
            u64 phys = pte & PTE_PHYS_MASK;
            u64 new_flags = PAGE_PRESENT | PAGE_USER;
            if (prot & 2 /* PROT_WRITE */) new_flags |= PAGE_WRITE;
            if (!(prot & 4 /* PROT_EXEC */)) new_flags |= PAGE_NX;
            if (prot == 0 /* PROT_NONE */) new_flags &= ~PAGE_PRESENT;
            *pte_ptr = phys | new_flags;
        }
    }

    smp_tlb_flush_range(start_va, end_va - start_va);

    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
    return 0;
}

bool pmap_handle_cow_fault(u64 pml4_phys, u64 fault_va) {
    if (!pml4_phys || fault_va >= 0x0000800000000000ULL) return false;

    extern u16 pmm_get_refcount(chimera_paddr_t addr);
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);

    u64 *pte_ptr = pmap_get_pte_ptr(pml4_phys, fault_va);
    if (!pte_ptr || !(*pte_ptr & PAGE_PRESENT) || !(*pte_ptr & PAGE_COW)) {
        spinlock_unlock_irqrestore(&s_pmap_lock, irq);
        return false;
    }

    u64 old_phys = *pte_ptr & PTE_PHYS_MASK;
    u16 refcount = pmm_get_refcount(old_phys);

    if (refcount > 1) {
        u64 new_phys = pmm_alloc_page();
        if (new_phys == 0 || new_phys == (u64)-1) {
            spinlock_unlock_irqrestore(&s_pmap_lock, irq);
            return false;
        }

        void *src_ptr = (void *)(old_phys + g_hhdm_base);
        void *dst_ptr = (void *)((new_phys & PTE_PHYS_MASK) + g_hhdm_base);
        __builtin_memcpy(dst_ptr, src_ptr, 4096);

        pmm_release_page(old_phys);
        *pte_ptr = (new_phys & PTE_PHYS_MASK) | (*pte_ptr & ~PTE_PHYS_MASK) | PAGE_WRITE;
        *pte_ptr &= ~PAGE_COW;
    } else {
        *pte_ptr = (*pte_ptr | PAGE_WRITE) & ~PAGE_COW;
    }

    __asm__ volatile("invlpg (%0)" :: "r"(fault_va) : "memory");
    smp_tlb_shootdown();

    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
    return true;
}

