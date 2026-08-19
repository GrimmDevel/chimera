/* =============================================================================
 * XIU Operating System — Physical Memory Manager
 * kernel/mm/pmm.c
 *
 * Stage 2: Bitmap allocator driven by the Limine memory map.
 * Only USABLE pages are marked free. Everything else (kernel ELF,
 * framebuffer MMIO, ACPI, bootloader reclaimable) starts as allocated.
 *
 * Thread-safety: all mutations hold s_pmm_lock (irqsave spinlock).
 * ============================================================================= */

#include <kernel/xiu_types.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <limine/limine.h>

#define MAX_PAGES (16 * 1024 * 1024) /* 64 GiB at 4 KiB pages */

static u8         s_pmm_bitmap[MAX_PAGES / 8];
static u8         s_pmm_refcount[MAX_PAGES];
static usize      s_total_pages = 0;
static usize      s_free_pages  = 0;
static spinlock_t s_pmm_lock    = SPINLOCK_INIT;
static usize      s_last_alloc_idx = 0;

// bitmap helpers
static inline void pmm_set_free(usize page) {
    s_pmm_bitmap[page / 8] &= (u8)~(1u << (page % 8));
    s_pmm_refcount[page] = 0;
}

/* ── pmm_init ────────────────────────────────────────────────────────────── *
 * Walk the Limine memory map.
 * ─────────────────────────────────────────────────────────────────────────── */
void pmm_init(xiu_paddr_t memmap_base, usize memmap_count) {
    s_total_pages = 0;
    s_free_pages  = 0;
    s_last_alloc_idx = 0;

    // step 1: Mark ALL pages used with refcount 1
    __builtin_memset(s_pmm_bitmap, 0xFF, sizeof(s_pmm_bitmap));
    __builtin_memset(s_pmm_refcount, 0, sizeof(s_pmm_refcount));

    // step 2: Walk Limine memory map and free USABLE regions
    struct limine_memmap_entry **entries =
        (struct limine_memmap_entry **)memmap_base;

    if (!entries || memmap_count == 0) {
        kprintf("[pmm] WARNING: no Limine memmap — using conservative fallback\n");
        s_total_pages = 1024 * 1024;
        for (usize i = 256; i < s_total_pages; i++) {
            pmm_set_free(i);
            s_free_pages++;
        }
        return;
    }

    u64 max_phys_addr = 0;
    for (usize e = 0; e < memmap_count; e++) {
        struct limine_memmap_entry *entry = entries[e];
        if (!entry) continue;

        if (entry->base + entry->length > max_phys_addr) {
            max_phys_addr = entry->base + entry->length;
        }

        if (entry->type != LIMINE_MEMMAP_USABLE) continue;

        u64 base = entry->base;
        u64 len  = entry->length;

        // skip legacy 0-1MB
        if (base < 0x100000ULL) {
            if (base + len <= 0x100000ULL) continue;
            u64 skip = 0x100000ULL - base;
            base += skip;
            len  -= skip;
        }

        usize first_page = (usize)(base / XIU_PAGE_SIZE);
        usize last_page  = (usize)((base + len) / XIU_PAGE_SIZE);

        if (first_page >= MAX_PAGES) continue;
        if (last_page > MAX_PAGES) last_page = MAX_PAGES;

        for (usize p = first_page; p < last_page; p++) {
            pmm_set_free(p);
            s_free_pages++;
        }
    }

    s_total_pages = (usize)(max_phys_addr / XIU_PAGE_SIZE);
    if (s_total_pages > MAX_PAGES) s_total_pages = MAX_PAGES;

    kprintf("[pmm] Limine memmap: %zu free pages (%zu MiB) of %zu total (max phys %llu GiB)\n",
            s_free_pages,
            (s_free_pages * XIU_PAGE_SIZE) / (1024 * 1024),
            s_total_pages,
            (unsigned long long)(max_phys_addr / (1024 * 1024 * 1024)));
}

usize pmm_total_pages(void) { return s_total_pages; }
usize pmm_free_pages(void) { return s_free_pages; }

xiu_paddr_t pmm_alloc_page(void) {
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmm_lock);
    usize limit = s_total_pages > 0 ? s_total_pages : MAX_PAGES;
    if (limit > MAX_PAGES) limit = MAX_PAGES;

    usize start = s_last_alloc_idx < limit ? s_last_alloc_idx : 0;
    for (usize i = start; i < limit; i++) {
        if (!(s_pmm_bitmap[i / 8] & (1 << (i % 8)))) {
            s_pmm_bitmap[i / 8] |= (1 << (i % 8));
            s_pmm_refcount[i] = 1;
            s_free_pages--;
            s_last_alloc_idx = i + 1;
            spinlock_unlock_irqrestore(&s_pmm_lock, irq);
            return (xiu_paddr_t)i * XIU_PAGE_SIZE;
        }
    }
    for (usize i = 0; i < start; i++) {
        if (!(s_pmm_bitmap[i / 8] & (1 << (i % 8)))) {
            s_pmm_bitmap[i / 8] |= (1 << (i % 8));
            s_pmm_refcount[i] = 1;
            s_free_pages--;
            s_last_alloc_idx = i + 1;
            spinlock_unlock_irqrestore(&s_pmm_lock, irq);
            return (xiu_paddr_t)i * XIU_PAGE_SIZE;
        }
    }
    spinlock_unlock_irqrestore(&s_pmm_lock, irq);
    return (xiu_paddr_t)-1;
}

xiu_paddr_t pmm_alloc_pages(usize count) {
    if (count == 0) return (xiu_paddr_t)-1;
    if (count == 1) return pmm_alloc_page();

    irq_flags_t irq = spinlock_lock_irqsave(&s_pmm_lock);
    usize limit = s_total_pages > 0 ? s_total_pages : MAX_PAGES;
    if (limit > MAX_PAGES) limit = MAX_PAGES;

    usize run = 0;
    usize start_idx = 0;
    for (usize i = 0; i < limit; i++) {
        if (!(s_pmm_bitmap[i / 8] & (1 << (i % 8)))) {
            if (run == 0) start_idx = i;
            run++;
            if (run == count) {
                for (usize j = start_idx; j < start_idx + count; j++) {
                    s_pmm_bitmap[j / 8] |= (1 << (j % 8));
                    s_pmm_refcount[j] = 1;
                }
                s_free_pages -= count;
                spinlock_unlock_irqrestore(&s_pmm_lock, irq);
                return (xiu_paddr_t)start_idx * XIU_PAGE_SIZE;
            }
        } else {
            run = 0;
        }
    }
    spinlock_unlock_irqrestore(&s_pmm_lock, irq);
    return (xiu_paddr_t)-1;
}

void pmm_retain_page(xiu_paddr_t addr) {
    usize idx = addr / XIU_PAGE_SIZE;
    if (idx >= MAX_PAGES) return;
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmm_lock);
    if (s_pmm_refcount[idx] < 255) {
        s_pmm_refcount[idx]++;
    }
    spinlock_unlock_irqrestore(&s_pmm_lock, irq);
}

void pmm_release_page(xiu_paddr_t addr) {
    usize idx = addr / XIU_PAGE_SIZE;
    if (idx >= MAX_PAGES) return;
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmm_lock);
    if (s_pmm_refcount[idx] > 0) {
        s_pmm_refcount[idx]--;
        if (s_pmm_refcount[idx] == 0) {
            if (s_pmm_bitmap[idx / 8] & (1 << (idx % 8))) {
                s_pmm_bitmap[idx / 8] &= ~(1 << (idx % 8));
                s_free_pages++;
            }
        }
    }
    spinlock_unlock_irqrestore(&s_pmm_lock, irq);
}

u16 pmm_get_refcount(xiu_paddr_t addr) {
    usize idx = addr / XIU_PAGE_SIZE;
    if (idx >= MAX_PAGES) return 0;
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmm_lock);
    u16 rc = s_pmm_refcount[idx];
    spinlock_unlock_irqrestore(&s_pmm_lock, irq);
    return rc;
}

void pmm_free_page(xiu_paddr_t addr) {
    pmm_release_page(addr);
}

void pmm_free_contiguous(xiu_paddr_t addr, usize count) {
    if (addr == (xiu_paddr_t)-1 || count == 0) return;
    for (usize i = 0; i < count; i++) {
        pmm_release_page(addr + (i * XIU_PAGE_SIZE));
    }
}


