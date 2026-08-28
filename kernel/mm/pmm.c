/* =============================================================================
 * Chimera Operating System — Physical Memory Manager (Darwin Mach VM Buddy Allocator)
 * kernel/mm/pmm.c
 *
 * Implements an O(1) Binary Buddy Allocator for physical pages with orders
 * 0..10 (4 KiB .. 4 MiB blocks). Tracks physical page descriptors (vm_page_t)
 * with strict reference counting for Copy-On-Write (COW) and zero leakage.
 * ============================================================================= */

#include <kernel/vm_page.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/chimera_types.h>
#include <limine/limine.h>

extern void kprintf(const char *fmt, ...);

#define BUDDY_MAX_ORDER 14     /* 2^14 = 16384 pages = 64 MiB */

static vm_page_t *s_pages = nullptr;
static vm_page_t *s_buddy_freelist[BUDDY_MAX_ORDER + 1];
static usize s_max_phys_page = 0;
static usize s_total_ram_pages = 0;
static usize s_free_pages = 0;
static spinlock_t s_pmm_lock = SPINLOCK_INIT;

// list helpers for buddy free lists
static inline void buddy_list_add(u32 order, vm_page_t *page) {
    page->order = (u16)order;
    page->flags = VM_PAGE_FREE;
    page->next = s_buddy_freelist[order];
    page->prev = nullptr;
    if (s_buddy_freelist[order]) {
        s_buddy_freelist[order]->prev = page;
    }
    s_buddy_freelist[order] = page;
}

static inline void buddy_list_remove(u32 order, vm_page_t *page) {
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        s_buddy_freelist[order] = page->next;
    }
    if (page->next) {
        page->next->prev = page->prev;
    }
    page->next = nullptr;
    page->prev = nullptr;
    page->flags = 0;
}

static void buddy_free_block(ppnum_t pfn, u32 order) {
    while (order < BUDDY_MAX_ORDER) {
        ppnum_t buddy_pfn = pfn ^ (1U << order);
        if (buddy_pfn >= s_max_phys_page) {
            break;
        }

        vm_page_t *buddy_page = &s_pages[buddy_pfn];
        if (!(buddy_page->flags & VM_PAGE_FREE) || buddy_page->order != order || buddy_page->ref_count != 0) {
            break;
        }

        // remove buddy from its freelist and merge
        buddy_list_remove(order, buddy_page);
        if (buddy_pfn < pfn) {
            pfn = buddy_pfn;
        }
        order++;
    }

    vm_page_t *merged_page = &s_pages[pfn];
    buddy_list_add(order, merged_page);
}

void pmm_init(chimera_paddr_t memmap_base, usize memmap_count) {
    s_max_phys_page = 0;
    s_total_ram_pages = 0;
    s_free_pages = 0;

    for (u32 o = 0; o <= BUDDY_MAX_ORDER; o++) {
        s_buddy_freelist[o] = nullptr;
    }

    struct limine_memmap_entry **entries = (struct limine_memmap_entry **)memmap_base;
    if (!entries || memmap_count == 0) {
        kprintf("[pmm] WARNING: no Limine memmap\n");
        return;
    }

    u64 max_phys_addr = 0;
    usize total_ram_bytes = 0;

    for (usize e = 0; e < memmap_count; e++) {
        struct limine_memmap_entry *entry = entries[e];
        if (!entry) continue;

        if (entry->base + entry->length > max_phys_addr) {
            max_phys_addr = entry->base + entry->length;
        }

        if (entry->type == LIMINE_MEMMAP_USABLE ||
            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            entry->type == LIMINE_MEMMAP_KERNEL_AND_MODULES ||
            entry->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE) {
            total_ram_bytes += entry->length;
        }
    }

    s_max_phys_page = (usize)(max_phys_addr / CHIMERA_PAGE_SIZE);
    s_total_ram_pages = (usize)(total_ram_bytes / CHIMERA_PAGE_SIZE);

    usize page_array_bytes = s_max_phys_page * sizeof(vm_page_t);
    usize page_array_pages = (page_array_bytes + CHIMERA_PAGE_SIZE - 1) / CHIMERA_PAGE_SIZE;

    // dynamically allocate page descriptor array from largest usable memory segment
    extern u64 g_hhdm_base;
    chimera_paddr_t array_paddr = 0;
    for (usize e = 0; e < memmap_count; e++) {
        struct limine_memmap_entry *entry = entries[e];
        if (!entry || entry->type != LIMINE_MEMMAP_USABLE) continue;
        if (entry->length >= page_array_bytes + 0x100000) {
            array_paddr = (entry->base >= 0x100000) ? entry->base : 0x100000;
            break;
        }
    }
    if (!array_paddr) {
        for (usize e = 0; e < memmap_count; e++) {
            struct limine_memmap_entry *entry = entries[e];
            if (entry && entry->type == LIMINE_MEMMAP_USABLE && entry->length >= page_array_bytes) {
                array_paddr = entry->base;
                break;
            }
        }
    }

    s_pages = (vm_page_t *)(array_paddr + g_hhdm_base);
    usize array_first_page = (usize)(array_paddr / CHIMERA_PAGE_SIZE);
    usize array_last_page = array_first_page + page_array_pages;

    // initialize all page descriptors as wired/used initially
    for (usize i = 0; i < s_max_phys_page; i++) {
        s_pages[i].phys_page = (ppnum_t)i;
        s_pages[i].ref_count = 1;
        s_pages[i].wire_count = 1;
        s_pages[i].flags = VM_PAGE_WIRED;
        s_pages[i].order = 0;
        s_pages[i].next = nullptr;
        s_pages[i].prev = nullptr;
    }

    // populate free pages from usable regions
    for (usize e = 0; e < memmap_count; e++) {
        struct limine_memmap_entry *entry = entries[e];
        if (!entry || entry->type != LIMINE_MEMMAP_USABLE) continue;

        u64 base = entry->base;
        u64 len = entry->length;

        // skip low 1MB legacy BIOS/VGA memory
        if (base < 0x100000ULL) {
            if (base + len <= 0x100000ULL) continue;
            u64 skip = 0x100000ULL - base;
            base += skip;
            len -= skip;
        }

        usize first_page = (usize)(base / CHIMERA_PAGE_SIZE);
        usize last_page = (usize)((base + len) / CHIMERA_PAGE_SIZE);
        if (last_page > s_max_phys_page) last_page = s_max_phys_page;

        for (usize p = first_page; p < last_page; p++) {
            if (p >= array_first_page && p < array_last_page) continue;
            s_pages[p].ref_count = 0;
            s_pages[p].wire_count = 0;
            buddy_free_block((ppnum_t)p, 0);
            s_free_pages++;
        }
    }

    if (s_total_ram_pages == 0) {
        s_total_ram_pages = s_free_pages;
    }

    kprintf("[pmm] Darwin Mach Buddy PMM: %zu free pages (%zu MiB) of %zu total RAM (%zu MiB)\n",
            s_free_pages, (s_free_pages * CHIMERA_PAGE_SIZE) / (1024 * 1024),
            s_total_ram_pages, (s_total_ram_pages * CHIMERA_PAGE_SIZE) / (1024 * 1024));
}

usize pmm_total_pages(void) {
    return s_total_ram_pages;
}

usize pmm_free_pages(void) {
    return s_free_pages;
}

static chimera_paddr_t pmm_alloc_order_unlocked(u32 order) {
    if (order > BUDDY_MAX_ORDER) return (chimera_paddr_t)-1;

    u32 cur_order = order;
    while (cur_order <= BUDDY_MAX_ORDER && s_buddy_freelist[cur_order] == nullptr) {
        cur_order++;
    }

    if (cur_order > BUDDY_MAX_ORDER) {
        return (chimera_paddr_t)-1;
    }

    vm_page_t *block = s_buddy_freelist[cur_order];
    buddy_list_remove(cur_order, block);

    // split blocks down to target order
    while (cur_order > order) {
        cur_order--;
        ppnum_t buddy_pfn = block->phys_page + (1U << cur_order);
        vm_page_t *buddy_page = &s_pages[buddy_pfn];
        buddy_list_add(cur_order, buddy_page);
    }

    usize num_pages = 1U << order;
    for (usize i = 0; i < num_pages; i++) {
        vm_page_t *p = &s_pages[block->phys_page + i];
        p->ref_count = 1;
        p->wire_count = 0;
        p->flags = VM_PAGE_ACTIVE;
        p->order = (u16)order;
    }

    s_free_pages -= num_pages;
    return (chimera_paddr_t)block->phys_page * CHIMERA_PAGE_SIZE;
}

chimera_paddr_t pmm_alloc_page(void) {
    irq_flags_t irq = spinlock_lock_irqsave(&s_pmm_lock);
    chimera_paddr_t addr = pmm_alloc_order_unlocked(0);
    spinlock_unlock_irqrestore(&s_pmm_lock, irq);
    return (addr == (chimera_paddr_t)-1) ? 0 : addr;
}

chimera_paddr_t pmm_alloc_pages(usize count) {
    if (count == 0) return (chimera_paddr_t)-1;
    if (count == 1) return pmm_alloc_page();

    u32 order = 0;
    while ((1U << order) < count) {
        order++;
        if (order > BUDDY_MAX_ORDER) return (chimera_paddr_t)-1;
    }

    irq_flags_t irq = spinlock_lock_irqsave(&s_pmm_lock);
    chimera_paddr_t addr = pmm_alloc_order_unlocked(order);
    spinlock_unlock_irqrestore(&s_pmm_lock, irq);
    return addr;
}

void pmm_retain_page(chimera_paddr_t addr) {
    usize idx = addr / CHIMERA_PAGE_SIZE;
    if (idx >= s_max_phys_page) return;

    irq_flags_t irq = spinlock_lock_irqsave(&s_pmm_lock);
    if (s_pages[idx].ref_count < 65535) {
        s_pages[idx].ref_count++;
    }
    spinlock_unlock_irqrestore(&s_pmm_lock, irq);
}

void pmm_release_page(chimera_paddr_t addr) {
    usize idx = addr / CHIMERA_PAGE_SIZE;
    if (idx >= s_max_phys_page) return;

    irq_flags_t irq = spinlock_lock_irqsave(&s_pmm_lock);
    if (s_pages[idx].ref_count > 0) {
        s_pages[idx].ref_count--;
        if (s_pages[idx].ref_count == 0) {
            s_pages[idx].wire_count = 0;
            s_pages[idx].flags = 0;
            buddy_free_block((ppnum_t)idx, 0);
            s_free_pages++;
        }
    }
    spinlock_unlock_irqrestore(&s_pmm_lock, irq);
}

u16 pmm_get_refcount(chimera_paddr_t addr) {
    usize idx = addr / CHIMERA_PAGE_SIZE;
    if (idx >= s_max_phys_page) return 0;

    irq_flags_t irq = spinlock_lock_irqsave(&s_pmm_lock);
    u16 rc = s_pages[idx].ref_count;
    spinlock_unlock_irqrestore(&s_pmm_lock, irq);
    return rc;
}

void pmm_free_page(chimera_paddr_t addr) {
    pmm_release_page(addr);
}

void pmm_free_contiguous(chimera_paddr_t addr, usize count) {
    if (addr == (chimera_paddr_t)-1 || addr == 0 || count == 0) return;
    for (usize i = 0; i < count; i++) {
        pmm_release_page(addr + (i * CHIMERA_PAGE_SIZE));
    }
}
