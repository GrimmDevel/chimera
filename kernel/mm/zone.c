/* =============================================================================
 * Chimera Operating System — Darwin Mach Zone Allocator & Kernel Dynamic Memory
 * kernel/mm/zone.c
 *
 * Implements Darwin zinit/zalloc/zfree zone subsystem and kalloc/kfree.
 * Elements are carved from physical pages allocated via PMM.
 * ============================================================================= */

#include <kernel/zone.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/chimera_types.h>

extern void kprintf(const char *fmt, ...);
extern chimera_paddr_t pmm_alloc_page(void);
extern chimera_paddr_t pmm_alloc_pages(usize count);
extern void pmm_release_page(chimera_paddr_t addr);
extern void pmm_free_contiguous(chimera_paddr_t addr, usize count);
extern u64 g_hhdm_base;

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
    chimera_paddr_t phys_base;
} large_header_t;

typedef struct zone_link {
    struct zone_link *next;
} zone_link_t;

#define MAX_ZONES 32
static zone_struct_t s_zone_table[MAX_ZONES];
static u32           s_zone_table_count = 0;
static spinlock_t    s_zone_table_lock = SPINLOCK_INIT;

#define KALLOC_ZONE_COUNT 9
static const usize s_kalloc_sizes[KALLOC_ZONE_COUNT] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

static const char *s_kalloc_names[KALLOC_ZONE_COUNT] = {
    "kalloc.16", "kalloc.32", "kalloc.64", "kalloc.128",
    "kalloc.256", "kalloc.512", "kalloc.1024", "kalloc.2048", "kalloc.4096"
};

static bool s_zone_initialized = false;

zone_t zinit(vm_size_t size, vm_size_t max, vm_size_t alloc, const char *name) {
    (void)max;
    if (size == 0) return ZONE_NULL;

    // align element size to 8 bytes minimum
    if (size < sizeof(void *)) size = sizeof(void *);
    size = (size + 7) & ~7ULL;

    if (alloc == 0) alloc = 4096;

    irq_flags_t irq = spinlock_lock_irqsave(&s_zone_table_lock);
    if (s_zone_table_count >= MAX_ZONES) {
        spinlock_unlock_irqrestore(&s_zone_table_lock, irq);
        return ZONE_NULL;
    }

    u32 idx = s_zone_table_count++;
    zone_t z = &s_zone_table[idx];
    z->z_name = name;
    z->z_elem_size = size;
    z->z_alloc_size = alloc;
    z->z_max_size = max;
    z->z_cur_size = 0;
    z->z_count = 0;
    z->z_free_count = 0;
    z->z_free_list = nullptr;
    spinlock_init(&z->z_lock);

    spinlock_unlock_irqrestore(&s_zone_table_lock, irq);
    return z;
}

zone_t zone_create(const char *name, vm_size_t size, vm_size_t flags) {
    (void)flags;
    return zinit(size, 0, 4096, name);
}

void *zalloc(zone_t zone) {
    if (!zone) return nullptr;

    irq_flags_t irq = spinlock_lock_irqsave(&zone->z_lock);

    if (!zone->z_free_list) {
        // allocate new page for zone elements
        chimera_paddr_t phys = pmm_alloc_page();
        if (phys == 0 || phys == (chimera_paddr_t)-1) {
            spinlock_unlock_irqrestore(&zone->z_lock, irq);
            return nullptr;
        }

        u8 *page_virt = (u8 *)(phys + g_hhdm_base);
        usize elem_size = zone->z_elem_size;
        usize count = 4096 / elem_size;

        for (usize i = 0; i < count; i++) {
            zone_link_t *link = (zone_link_t *)(page_virt + (i * elem_size));
            link->next = (zone_link_t *)zone->z_free_list;
            zone->z_free_list = link;
            zone->z_free_count++;
        }
        zone->z_cur_size += 4096;
    }

    zone_link_t *item = (zone_link_t *)zone->z_free_list;
    zone->z_free_list = item->next;
    zone->z_free_count--;
    zone->z_count++;

    spinlock_unlock_irqrestore(&zone->z_lock, irq);

    __builtin_memset(item, 0, zone->z_elem_size);
    return (void *)item;
}

void *zalloc_noblock(zone_t zone) {
    return zalloc(zone);
}

void zfree(zone_t zone, void *elem) {
    if (!zone || !elem) return;

    irq_flags_t irq = spinlock_lock_irqsave(&zone->z_lock);

    zone_link_t *link = (zone_link_t *)elem;
    link->next = (zone_link_t *)zone->z_free_list;
    zone->z_free_list = link;
    zone->z_free_count++;
    if (zone->z_count > 0) {
        zone->z_count--;
    }

    spinlock_unlock_irqrestore(&zone->z_lock, irq);
}

void zone_init(void) {
    if (s_zone_initialized) return;

    spinlock_init(&s_zone_table_lock);
    s_zone_table_count = 0;

    for (int i = 0; i < KALLOC_ZONE_COUNT; i++) {
        zinit(s_kalloc_sizes[i], 0, 4096, s_kalloc_names[i]);
    }

    s_zone_initialized = true;
    kprintf("        zone: Darwin Mach Zone Allocator initialized (%d kalloc zones)\n",
            KALLOC_ZONE_COUNT);
}

void *kalloc(usize size) {
    if (size == 0) return nullptr;

    if (!s_zone_initialized) {
        zone_init();
    }

    usize needed = size + sizeof(zone_header_t);

    if (needed <= 4096) {
        int zi = -1;
        for (int i = 0; i < KALLOC_ZONE_COUNT; i++) {
            if (s_zone_table[i].z_elem_size >= needed) {
                zi = i;
                break;
            }
        }

        if (zi >= 0) {
            zone_t z = &s_zone_table[zi];
            void *mem = zalloc(z);
            if (!mem) return nullptr;

            zone_header_t *hdr = (zone_header_t *)mem;
            hdr->magic = ZONE_MAGIC;
            hdr->zone_idx = (u32)zi;
            hdr->alloc_size = (u32)size;
            return (void *)(hdr + 1);
        }
    }

    // large allocation via whole pages
    usize total = size + sizeof(large_header_t);
    usize pages = (total + 4095) / 4096;
    chimera_paddr_t phys = pmm_alloc_pages(pages);
    if (phys == (chimera_paddr_t)-1 || phys == 0) return nullptr;

    large_header_t *lhdr = (large_header_t *)(phys + g_hhdm_base);
    lhdr->magic = LARGE_MAGIC;
    lhdr->page_count = pages;
    lhdr->phys_base = phys;

    void *ptr = (void *)(lhdr + 1);
    __builtin_memset(ptr, 0, size);
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;

    u8 *raw = (u8 *)ptr;
    zone_header_t *zh = (zone_header_t *)(raw - sizeof(zone_header_t));
    if (zh->magic == ZONE_MAGIC) {
        u32 zi = zh->zone_idx;
        if (zi < KALLOC_ZONE_COUNT) {
            zh->magic = 0;
            zfree(&s_zone_table[zi], zh);
            return;
        }
    }

    large_header_t *lh = (large_header_t *)(raw - sizeof(large_header_t));
    if (lh->magic == LARGE_MAGIC) {
        usize pages = lh->page_count;
        chimera_paddr_t phys = lh->phys_base;
        lh->magic = 0;
        pmm_free_contiguous(phys, pages);
        return;
    }
}
