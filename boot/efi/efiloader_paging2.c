
#define PAGE_2M_SIZE (2 * 1024 * 1024ULL)
#define PAGE_PS      (1ULL << 7)

static void MapPage2M(struct EFI_SYSTEM_TABLE* st, uint64_t* pml4, uint64_t vaddr, uint64_t paddr) {
    uint64_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (vaddr >> 30) & 0x1FF;
    uint64_t pd_idx   = (vaddr >> 21) & 0x1FF;

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

    pd[pd_idx] = paddr | PAGE_PRESENT | PAGE_RW | PAGE_PS;
}

