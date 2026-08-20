/* =============================================================================
 * XIU Operating System — Zone Allocator & Kernel Dynamic Memory
 * kernel/mm/zone.c
 * ============================================================================= */

#include <kernel/xiu_types.h>
#include <kernel/spinlock.h>
#include <kernel/panic.h>

extern void kprintf(const char *fmt, ...);
extern xiu_paddr_t pmm_alloc_page(void);
extern xiu_paddr_t pmm_alloc_pages(usize count);
extern void pmm_free_page(xiu_paddr_t addr);
extern void pmm_free_contiguous(xiu_paddr_t addr, usize count);

#define ZONE_MAGIC   0x585A4F4E45484452ULL
#define LARGE_MAGIC  0x584C415247454844ULL

typedef struct {
    u64 magic;
    u32 zone_idx;
    u32 alloc_size;
} zone_header_t;

typedef struct {
    u64 magic;
    usize page_count;
    xiu_paddr_t phys_base;
} large_header_t;

typedef struct zone_free_block {
    struct zone_free_block *next;
} zone_free_block_t;

typedef struct {
    const char         *name;
    usize               elem_size;
    spinlock_t          lock;
    zone_free_block_t  *free_list;
    usize               total_pages;
    usize               free_count;
} zone_t;

#define ZONE_COUNT 9
static const usize s_zone_sizes[ZONE_COUNT] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

static const char *s_zone_names[ZONE_COUNT] = {
    "zone.16", "zone.32", "zone.64", "zone.128",
    "zone.256", "zone.512", "zone.1024", "zone.2048", "zone.4096"
};

static zone_t s_zones[ZONE_COUNT];
static bool   s_zone_initialized = false;

void zone_init(void) {
    for (int i = 0; i < ZONE_COUNT; i++) {
        s_zones[i].name        = s_zone_names[i];
        s_zones[i].elem_size   = s_zone_sizes[i];
        s_zones[i].free_list   = nullptr;
        s_zones[i].total_pages = 0;
        s_zones[i].free_count  = 0;
        spinlock_init(&s_zones[i].lock);
    }
    s_zone_initialized = true;
    kprintf("        zone: initialized %d size-class zones (16B .. 4096B)\n", ZONE_COUNT);
}

void *kalloc(usize size) {
    if (size == 0) return nullptr;

    if (!s_zone_initialized) {
        zone_init();
    }

    usize needed = size + sizeof(zone_header_t);

    // zone bucket allocation
    if (needed <= 4096) {
        int zi = -1;
        for (int i = 0; i < ZONE_COUNT; i++) {
            if (s_zones[i].elem_size >= needed) {
                zi = i;
                break;
            }
        }

        if (zi >= 0) {
            zone_t *z = &s_zones[zi];
            irq_flags_t irq = spinlock_lock_irqsave(&z->lock);

            if (!z->free_list) {
                xiu_paddr_t phys = pmm_alloc_page();
                if (phys == (xiu_paddr_t)-1 || phys == 0) {
                    spinlock_unlock_irqrestore(&z->lock, irq);
                    return nullptr;
                }

                u8 *page_virt = (u8 *)(phys + g_hhdm_base);
                __builtin_memset(page_virt, 0, 4096);
                z->total_pages++;

                usize count = 4096 / z->elem_size;
                for (usize b = 0; b < count; b++) {
                    zone_free_block_t *blk = (zone_free_block_t *)(page_virt + (b * z->elem_size));
                    blk->next = z->free_list;
                    z->free_list = blk;
                    z->free_count++;
                }
            }

            zone_free_block_t *res = z->free_list;
            z->free_list = res->next;
            z->free_count--;
            spinlock_unlock_irqrestore(&z->lock, irq);

            zone_header_t *hdr = (zone_header_t *)res;
            hdr->magic      = ZONE_MAGIC;
            hdr->zone_idx   = (u32)zi;
            hdr->alloc_size = (u32)size;

            return (void *)((uptr)hdr + sizeof(zone_header_t));
        }
    }

    // large page allocation
    usize total_bytes = size + sizeof(large_header_t);
    usize pages = (total_bytes + 4095) / 4096;
    xiu_paddr_t phys = pmm_alloc_pages(pages);
    if (phys == (xiu_paddr_t)-1 || phys == 0) {
        return nullptr;
    }

    large_header_t *lhdr = (large_header_t *)(phys + g_hhdm_base);
    lhdr->magic      = LARGE_MAGIC;
    lhdr->page_count = pages;
    lhdr->phys_base  = phys;

    return (void *)((uptr)lhdr + sizeof(large_header_t));
}

void kfree(void *ptr) {
    if (!ptr) return;

    zone_header_t *zh = (zone_header_t *)((uptr)ptr - sizeof(zone_header_t));
    if (zh->magic == ZONE_MAGIC) {
        u32 zi = zh->zone_idx;
        if (zi < ZONE_COUNT) {
            zone_t *z = &s_zones[zi];
            zone_free_block_t *blk = (zone_free_block_t *)zh;

            irq_flags_t irq = spinlock_lock_irqsave(&z->lock);
            zh->magic = 0; // invalidate
            blk->next = z->free_list;
            z->free_list = blk;
            z->free_count++;
            spinlock_unlock_irqrestore(&z->lock, irq);
            return;
        }
    }

    large_header_t *lh = (large_header_t *)((uptr)ptr - sizeof(large_header_t));
    if (lh->magic == LARGE_MAGIC) {
        xiu_paddr_t phys = lh->phys_base;
        usize pages      = lh->page_count;
        lh->magic = 0;
        pmm_free_contiguous(phys, pages);
        return;
    }
}
