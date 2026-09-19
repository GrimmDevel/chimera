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
#define PAGE_PWT     (1ULL << 3)
#define PAGE_PCD     (1ULL << 4)
#define PAGE_USER    (1ULL << 2)
#define PAGE_COW     (1ULL << 9)  /* Available for OS: Copy-on-Write bit */
#define PTE_PHYS_MASK 0x000FFFFFFFFFF000ULL

extern void pmm_retain_page(chimera_paddr_t addr);
extern void pmm_release_page(chimera_paddr_t addr);

static u64 s_kernel_pml4_phys = 0;
static spinlock_t s_pmap_lock = SPINLOCK_INIT;

// ── signal trampoline ("mini-vdso") ──────────────────────────────────────────
// One global physical page holding a static entry stub, mapped USER+RX into
// every address space at a fixed VA. Signal delivery only places DATA (sig,
// handler, ctx pointer) on the user's (NX) stack and redirects execution to
// this page; executable code must never live on the stack itself.
static u64 s_tramp_phys = 0;

// static stub, reads everything from the per-delivery stack slots:
//   mov rdi, [rsp]        ; sig
//   call qword [rsp+8]    ; handler(sig)
//   mov rdi, [rsp+16]     ; &sigctx
//   mov eax, 184          ; SYS_sigreturn
//   syscall
static const u8 s_tramp_code[] = {
    0x48, 0x8B, 0x3C, 0x24,
    0xFF, 0x54, 0x24, 0x08,
    0x48, 0x8B, 0x7C, 0x24, 0x10,
    0xB8, 0xB8, 0x00, 0x00, 0x00,
    0x0F, 0x05
};

static u64 pmap_map_user_page_unlocked(u64 target_pml4_phys, u64 vaddr,
                                       u64 paddr, u64 flags);
static inline u64 *get_table_ptr(u64 phys);

static inline u64 va_of(u64 pml4_i, u64 pdpt_i, u64 pd_i, u64 pt_i) {
    return (pml4_i << 39) | (pdpt_i << 30) | (pd_i << 21) | (pt_i << 12);
}

bool pmap_is_trampoline_va(u64 va) {
    return (va & ~0xFFFULL) == SIGNAL_TRAMP_VA;
}

void pmap_trampoline_install(u64 pml4_phys) {
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);

    if (s_tramp_phys == 0) {
        u64 phys = pmm_alloc_page();
        if (phys == 0 || phys == (u64)-1) {
            spinlock_unlock_irqrestore(&s_pmap_lock, irq);
            kprintf("pmap: out of memory for signal trampoline page\n");
            return;
        }
        u8 *dst = (u8 *)(phys + g_hhdm_base);
        __builtin_memset(dst, 0x90, 4096); // nop-fill for safe overruns
        __builtin_memcpy(dst, s_tramp_code, sizeof(s_tramp_code));
        s_tramp_phys = phys;
        // one permanent reference: the page is never released via PTE sweeps
        pmm_retain_page(phys);
    }

    // USER+RX: present, no write, no NX
    pmap_map_user_page_unlocked(pml4_phys, SIGNAL_TRAMP_VA, s_tramp_phys,
                                PAGE_USER);
    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
}

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

/*
 * Map device memory into the kernel's direct-map address range.  Firmware
 * commonly places PCI BARs above installed RAM; those addresses are not part
 * of the bootloader-created HHDM and must be explicitly mapped before a
 * driver dereferences them.
 */
void *pmap_map_kernel_mmio(u64 paddr, usize size) {
    if (!s_kernel_pml4_phys || size == 0) return nullptr;

    const u64 page_offset = paddr & 0xFFFULL;
    const u64 phys_start = paddr & PTE_PHYS_MASK;
    const u64 page_count = (page_offset + size + 0xFFFULL) >> 12;
    const u64 virt_start = g_hhdm_base + phys_start;
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);

    u64 *pml4 = get_table_ptr(s_kernel_pml4_phys);
    for (u64 page = 0; page < page_count; page++) {
        u64 va = virt_start + (page << 12);
        u64 pa = phys_start + (page << 12);
        u64 *pml4e = &pml4[(va >> 39) & 0x1FF];
        if (!(*pml4e & PAGE_PRESENT)) {
            u64 table_phys = pmm_alloc_page();
            if (table_phys == 0 || table_phys == (u64)-1) goto failed;
            __builtin_memset(get_table_ptr(table_phys), 0, 4096);
            *pml4e = table_phys | PAGE_PRESENT | PAGE_WRITE;
        }

        u64 *pdpt = get_table_ptr(*pml4e);
        u64 *pdpte = &pdpt[(va >> 30) & 0x1FF];
        if (!(*pdpte & PAGE_PRESENT)) {
            u64 table_phys = pmm_alloc_page();
            if (table_phys == 0 || table_phys == (u64)-1) goto failed;
            __builtin_memset(get_table_ptr(table_phys), 0, 4096);
            *pdpte = table_phys | PAGE_PRESENT | PAGE_WRITE;
        } else if (*pdpte & (1ULL << 7)) {
            continue;
        }

        u64 *pd = get_table_ptr(*pdpte);
        u64 *pde = &pd[(va >> 21) & 0x1FF];
        if (!(*pde & PAGE_PRESENT)) {
            u64 table_phys = pmm_alloc_page();
            if (table_phys == 0 || table_phys == (u64)-1) goto failed;
            __builtin_memset(get_table_ptr(table_phys), 0, 4096);
            *pde = table_phys | PAGE_PRESENT | PAGE_WRITE;
        } else if (*pde & (1ULL << 7)) {
            continue;
        }

        u64 *pt = get_table_ptr(*pde);
        pt[(va >> 12) & 0x1FF] = pa | PAGE_PRESENT | PAGE_WRITE |
                                  PAGE_PWT | PAGE_PCD;
        __asm__ volatile("invlpg (%0)" : : "r"(va) : "memory");
    }

    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
    return (void *)(virt_start + page_offset);

failed:
    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
    return nullptr;
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

    pmap_trampoline_install(pml4_phys);
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

static u64 pmap_map_user_page_unlocked(u64 target_pml4_phys, u64 vaddr, u64 paddr, u64 flags) {
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
    // Replacing an already-present PTE: drop the old mapping's reference so
    // fixed-address remaps stop leaking physical pages. Skip when remapping
    // the very same frame (e.g. framebuffer re-mmap).
    u64 old_pte = pt[pt_idx];
    if ((old_pte & PAGE_PRESENT) && (old_pte & PTE_PHYS_MASK) != (paddr & PTE_PHYS_MASK) &&
        !pmap_is_trampoline_va(vaddr)) {
        pmm_release_page(old_pte & PTE_PHYS_MASK);
    }
    pt[pt_idx] = (paddr & PTE_PHYS_MASK) | flags | PAGE_PRESENT | PAGE_USER;
    return paddr;
}

u64 pmap_map_user_page(u64 target_pml4_phys, u64 vaddr, u64 paddr, u64 flags) {
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);
    u64 ret = pmap_map_user_page_unlocked(target_pml4_phys, vaddr, paddr, flags);
    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
    return ret;
}

// pmap_vtophys: robust virtual to physical translation for ANY address (including kernel/HHDM)
u64 pmap_vtophys(u64 pml4_phys, u64 vaddr) {
    if (!pml4_phys) return 0;
    
    // Fast path for HHDM
    if (vaddr >= g_hhdm_base && vaddr < (g_hhdm_base + 0x400000000000ULL)) {
        return vaddr - g_hhdm_base;
    }
    
    u64 *pml4 = get_table_ptr(pml4_phys);
    u64 pml4e = pml4[(vaddr >> 39) & 0x1FF];
    if (!(pml4e & PAGE_PRESENT)) return 0;

    u64 *pdpt = get_table_ptr(pml4e);
    u64 pdpte = pdpt[(vaddr >> 30) & 0x1FF];
    if (!(pdpte & PAGE_PRESENT)) return 0;
    
    if (pdpte & (1ULL << 7)) { // 1GB page
        return (pdpte & 0x000FFFFFC0000000ULL) | (vaddr & 0x3FFFFFFFULL);
    }

    u64 *pd = get_table_ptr(pdpte);
    u64 pde = pd[(vaddr >> 21) & 0x1FF];
    if (!(pde & PAGE_PRESENT)) return 0;
    
    if (pde & (1ULL << 7)) { // 2MB page
        return (pde & 0x000FFFFFFFE00000ULL) | (vaddr & 0x1FFFFFULL);
    }

    u64 *pt = get_table_ptr(pde);
    u64 pte = pt[(vaddr >> 12) & 0x1FF];
    if (!(pte & PAGE_PRESENT)) return 0;
    
    return (pte & PTE_PHYS_MASK) | (vaddr & 0xFFFULL);
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

    spinlock_unlock_irqrestore(&s_pmap_lock, irq);

    smp_tlb_shootdown_pml4(src_pml4_phys);

    pmap_trampoline_install(dst_pml4_phys);
    return dst_pml4_phys;
}

/* ── Full recursive destruction of a user address space ─────────────────── *
 * Frees all user physical pages and all intermediate page table pages.
 * ─────────────────────────────────────────────────────────────────────────── */
void pmap_destroy_user_space(u64 pml4_phys) {
    if (!pml4_phys) return;

    // switch to master kernel PML4 if current CPU is executing on this map
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if ((cr3 & PTE_PHYS_MASK) == (pml4_phys & PTE_PHYS_MASK)) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(s_kernel_pml4_phys) : "memory");
        extern u32 smp_current_cpu_id(void);
        extern cpu_local_t g_cpu_data[];
        u32 my_cpu = smp_current_cpu_id();
        if (my_cpu < CHIMERA_MAX_CPUS) {
            atomic_store_explicit(&g_cpu_data[my_cpu].cpu_active_cr3,
                                  s_kernel_pml4_phys & 0x000FFFFFFFFFF000ULL,
                                  memory_order_release);
        }
    }

    // Synchronously shoot down TLB for this PML4 across all cores before
    // releasing physical pages back to the allocator. Done outside s_pmap_lock
    // so receiver cores do not deadlock if spinning on s_pmap_lock.
    smp_tlb_shootdown_pml4(pml4_phys);

    irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);

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
                    if (!pmap_is_trampoline_va(
                            va_of(pml4_i, pdpt_i, pd_i, pt_i)))
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

    spinlock_unlock_irqrestore(&s_pmap_lock, irq);
}

void pmap_unmap_user_range_ex(u64 pml4_phys, u64 vaddr, usize len, bool release_pages) {
    if (!pml4_phys || len == 0) return;

    u64 start = vaddr & ~0xFFFULL;
    u64 end = (vaddr + len + 4095) & ~0xFFFULL;

    // Process in batches of up to 64 pages:
    // 1. Zero PTEs under spinlock and collect physical addresses
    // 2. Release spinlock
    // 3. Shootdown TLB range across cores synchronously
    // 4. Release physical frames back to PMM buddy allocator
    for (u64 chunk_start = start; chunk_start < end; ) {
        u64 chunk_end = chunk_start + (64 * 4096);
        if (chunk_end > end) chunk_end = end;

        u64 phys_to_free[64];
        usize free_count = 0;

        irq_flags_t irq = spinlock_lock_irqsave(&s_pmap_lock);
        for (u64 va = chunk_start; va < chunk_end; va += 4096) {
            u64 *pte_ptr = pmap_get_pte_ptr(pml4_phys, va);
            if (pte_ptr && (*pte_ptr & PAGE_PRESENT)) {
                u64 phys = *pte_ptr & PTE_PHYS_MASK;
                *pte_ptr = 0;
                if (release_pages && !pmap_is_trampoline_va(va)) {
                    phys_to_free[free_count++] = phys;
                }
            }
        }
        spinlock_unlock_irqrestore(&s_pmap_lock, irq);

        smp_tlb_flush_range_pml4(pml4_phys, chunk_start, chunk_end - chunk_start);

        for (usize i = 0; i < free_count; i++) {
            pmm_release_page(phys_to_free[i]);
        }

        chunk_start = chunk_end;
    }
}

void pmap_unmap_user_range(u64 pml4_phys, u64 vaddr, usize len) {
    pmap_unmap_user_range_ex(pml4_phys, vaddr, len, true);
}

void pmap_clear_user_mappings(u64 pml4_phys) {
    if (!pml4_phys) return;
    smp_tlb_shootdown_pml4(pml4_phys);

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
                    if (!pmap_is_trampoline_va(
                            va_of(pml4_i, pdpt_i, pd_i, pt_i)))
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

    spinlock_unlock_irqrestore(&s_pmap_lock, irq);

    smp_tlb_flush_range_pml4(pml4_phys, start_va, end_va - start_va);
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

    spinlock_unlock_irqrestore(&s_pmap_lock, irq);

    smp_tlb_flush_page_pml4(pml4_phys, fault_va);
    return true;
}
