
// Paging definitions
#define PAGE_PRESENT (1ULL << 0)
#define PAGE_RW      (1ULL << 1)
#define PAGE_SIZE    4096

static void* AllocateZeroedPage(struct EFI_SYSTEM_TABLE* st) {
    EFI_PHYSICAL_ADDRESS Page;
    EFI_STATUS Status = st->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &Page);
    if (Status != EFI_SUCCESS) return NULL;
    memset((void*)Page, 0, PAGE_SIZE);
    return (void*)Page;
}

static void MapPage(struct EFI_SYSTEM_TABLE* st, uint64_t* pml4, uint64_t vaddr, uint64_t paddr) {
    uint64_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (vaddr >> 30) & 0x1FF;
    uint64_t pd_idx   = (vaddr >> 21) & 0x1FF;
    uint64_t pt_idx   = (vaddr >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        uint64_t* pdpt = AllocateZeroedPage(st);
        pml4[pml4_idx] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_RW;
    }
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        uint64_t* pd = AllocateZeroedPage(st);
        pdpt[pdpt_idx] = (uint64_t)pd | PAGE_PRESENT | PAGE_RW;
    }
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        uint64_t* pt = AllocateZeroedPage(st);
        pd[pd_idx] = (uint64_t)pt | PAGE_PRESENT | PAGE_RW;
    }
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFFULL);

    pt[pt_idx] = paddr | PAGE_PRESENT | PAGE_RW;
}

