#include <stdint.h>
#include <stddef.h>

#define EFI_SUCCESS 0
#define EFI_UNSUPPORTED ((1ULL << 63) | 3)
#define EFI_LOAD_ERROR  ((1ULL << 63) | 1)

typedef uint64_t EFI_STATUS;
typedef void* EFI_HANDLE;
typedef uint64_t EFI_PHYSICAL_ADDRESS;

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    uint32_t Type;
    uint32_t Padding;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* Reset;
    EFI_STATUS (*OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This, uint16_t* String);
    void* TestString;
    void* QueryMode;
    void* SetMode;
    void* SetAttribute;
    EFI_STATUS (*ClearScreen)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This);
    void* SetCursorPosition;
    void* EnableCursor;
    void* Mode;
};

typedef uint64_t uintN_t;

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} EFI_GUID;

struct EFI_BOOT_SERVICES {
    char _pad1[24]; // header
    char _pad2[16]; // TPL
    
    // Memory Services
    EFI_STATUS (*AllocatePages)(EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, uintN_t Pages, EFI_PHYSICAL_ADDRESS* Memory);
    EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS Memory, uintN_t Pages);
    EFI_STATUS (*GetMemoryMap)(uintN_t* MemoryMapSize, void* MemoryMap, uintN_t* MapKey, uintN_t* DescriptorSize, uint32_t* DescriptorVersion);
    EFI_STATUS (*AllocatePool)(EFI_MEMORY_TYPE PoolType, uintN_t Size, void** Buffer);
    EFI_STATUS (*FreePool)(void* Buffer);

    // Event & Timer Services
    void* CreateEvent;
    void* SetTimer;
    void* WaitForEvent;
    void* SignalEvent;
    void* CloseEvent;
    void* CheckEvent;
    
    // Protocol Handler Services
    void* InstallProtocolInterface;
    void* ReinstallProtocolInterface;
    void* UninstallProtocolInterface;
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE Handle, EFI_GUID* Protocol, void** Interface);
    void* Reserved;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    void* InstallConfigurationTable;
    
    // Image Services
    void* LoadImage;
    void* StartImage;
    void* Exit;
    void* UnloadImage;
    EFI_STATUS (*ExitBootServices)(EFI_HANDLE ImageHandle, uintN_t MapKey);
    
    // Misc Services
    void* GetNextMonotonicCount;
    void* Stall;
    void* SetWatchdogTimer;
    
    // DriverSupport Services
    void* ConnectController;
    void* DisconnectController;
    
    // Open and Close Protocol Services
    EFI_STATUS (*OpenProtocol)(EFI_HANDLE Handle, EFI_GUID* Protocol, void** Interface, EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle, uint32_t Attributes);
    void* CloseProtocol;
    void* OpenProtocolInformation;

    // Library Services
    void* ProtocolsPerHandle;
    void* LocateHandleBuffer;
    EFI_STATUS (*LocateProtocol)(EFI_GUID* Protocol, void* Registration, void** Interface);
};

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    uint32_t RedMask;
    uint32_t GreenMask;
    uint32_t BlueMask;
    uint32_t ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    uintN_t SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    uintN_t FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (*QueryMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL* This, uint32_t ModeNumber, uintN_t* SizeOfInfo, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION** Info);
    EFI_STATUS (*SetMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL* This, uint32_t ModeNumber);
    void* Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
};

struct EFI_CONFIGURATION_TABLE {
    EFI_GUID VendorGuid;
    void* VendorTable;
};

// EFI_FILE_PROTOCOL
struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (*Open)(struct EFI_FILE_PROTOCOL* This, struct EFI_FILE_PROTOCOL** NewHandle, uint16_t* FileName, uint64_t OpenMode, uint64_t Attributes);
    EFI_STATUS (*Close)(struct EFI_FILE_PROTOCOL* This);
    EFI_STATUS (*Delete)(struct EFI_FILE_PROTOCOL* This);
    EFI_STATUS (*Read)(struct EFI_FILE_PROTOCOL* This, uintN_t* BufferSize, void* Buffer);
    EFI_STATUS (*Write)(struct EFI_FILE_PROTOCOL* This, uintN_t* BufferSize, void* Buffer);
    EFI_STATUS (*GetPosition)(struct EFI_FILE_PROTOCOL* This, uint64_t* Position);
    EFI_STATUS (*SetPosition)(struct EFI_FILE_PROTOCOL* This, uint64_t Position);
    EFI_STATUS (*GetInfo)(struct EFI_FILE_PROTOCOL* This, EFI_GUID* InformationType, uintN_t* BufferSize, void* Buffer);
    EFI_STATUS (*SetInfo)(struct EFI_FILE_PROTOCOL* This, EFI_GUID* InformationType, uintN_t BufferSize, void* Buffer);
    EFI_STATUS (*Flush)(struct EFI_FILE_PROTOCOL* This);
};

// EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (*OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* This, struct EFI_FILE_PROTOCOL** Root);
};

// EFI_LOADED_IMAGE_PROTOCOL
struct EFI_LOADED_IMAGE_PROTOCOL {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    struct EFI_SYSTEM_TABLE* SystemTable;
    EFI_HANDLE DeviceHandle;
    void* FilePath;
    void* Reserved;
    uint32_t LoadOptionsSize;
    void* LoadOptions;
    void* ImageBase;
    uint64_t ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    EFI_STATUS (*Unload)(EFI_HANDLE ImageHandle);
};

#define EFI_FILE_MODE_READ 1
#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL 0x00000001

struct EFI_SYSTEM_TABLE {
    char _pad1[24]; // header
    uint16_t* FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void* ConIn;
    EFI_HANDLE ConsoleOutHandle;
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE StandardErrorHandle;
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* StdErr;
    void* RuntimeServices;
    struct EFI_BOOT_SERVICES* BootServices;
    uintN_t NumberOfTableEntries;
    struct EFI_CONFIGURATION_TABLE* ConfigurationTable;
};

void Print(struct EFI_SYSTEM_TABLE* st, const uint16_t* str) {
    st->ConOut->OutputString(st->ConOut, (uint16_t*)str);
}

// Mach-O definitions
#define MH_MAGIC_64 0xfeedfacf
#define LC_SEGMENT_64 0x19
#define LC_UNIXTHREAD 0x5

struct mach_header_64 {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct load_command {
    uint32_t cmd;
    uint32_t cmdsize;
};

struct segment_command_64 {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct thread_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t flavor;
    uint32_t count;
    uint64_t rax, rbx, rcx, rdx, rdi, rsi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags, cs, fs, gs;
};

// Chimera native memory map
typedef enum {
    CHIMERA_MEM_RESERVED = 0,
    CHIMERA_MEM_USABLE = 1,
    CHIMERA_MEM_ACPI_RECLAIM = 2,
    CHIMERA_MEM_ACPI_NVS = 3,
    CHIMERA_MEM_BOOTLOADER_RECLAIM = 4,
    CHIMERA_MEM_KERNEL = 5,
    CHIMERA_MEM_FRAMEBUFFER = 6
} chimera_mem_type_t;

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved_pad;
} chimera_memmap_entry_t;

// chimera_boot_info_t definition
typedef struct __attribute__((packed)) {
    uint64_t magic;
    uint32_t version;
    uint32_t flags;
    uint64_t memmap_base;
    uint64_t memmap_count;
    uint64_t memmap_desc_size;
    uint64_t kernel_phys_start;
    uint64_t kernel_phys_end;
    uint64_t kernel_virt_start;
    uint64_t fb_base;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_stride;
    uint32_t fb_format;
    uint32_t fb_bpp;
    uint32_t fb_pitch;
    uint64_t rsdp_base;
    uint64_t smbios_base;
    char cmdline[256];
} chimera_boot_info_t;

#define CHIMERA_BOOT_MAGIC 0x584955424F4F5421ULL

#define EFI_SIZE_TO_PAGES(Size)  (((Size) + 4095) / 4096)

static void* memset(void* s, int c, size_t n) {
    unsigned char* p = s;
    while(n--) *p++ = (unsigned char)c;
    return s;
}

static void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = dest;
    const unsigned char* s = src;
    while(n--) *d++ = *s++;
    return dest;
}


EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {0x5B1B31A1, 0x9562, 0x11d2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {0x964E5B22, 0x6459, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

EFI_STATUS ReadFile(struct EFI_SYSTEM_TABLE* st, EFI_HANDLE ImageHandle, uint16_t* Path, void** BufferOut, uintN_t* SizeOut) {
    struct EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
    EFI_STATUS Status = st->BootServices->OpenProtocol(ImageHandle, &EFI_LOADED_IMAGE_PROTOCOL_GUID, (void**)&LoadedImage, ImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Failed to get LoadedImage protocol\r\n");
        return Status;
    }

    struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;
    Status = st->BootServices->OpenProtocol(LoadedImage->DeviceHandle, &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, (void**)&FileSystem, ImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Failed to get FileSystem protocol\r\n");
        return Status;
    }

    struct EFI_FILE_PROTOCOL* Root;
    Status = FileSystem->OpenVolume(FileSystem, &Root);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Failed to open volume\r\n");
        return Status;
    }

    struct EFI_FILE_PROTOCOL* File;
    Status = Root->Open(Root, &File, Path, EFI_FILE_MODE_READ, 0);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Failed to open file\r\n");
        return Status;
    }

    // Get file size
    EFI_GUID FileInfoGuid = {0x09576E92, 0x6D3F, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};
    char InfoBuffer[256];
    uintN_t InfoSize = sizeof(InfoBuffer);
    Status = File->GetInfo(File, &FileInfoGuid, &InfoSize, InfoBuffer);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Failed to get file info\r\n");
        return Status;
    }

    // EFI_FILE_INFO struct: FileSize is at offset 8 (uint64_t)
    uint64_t FileSize = *(uint64_t*)(InfoBuffer + 8);

    void* FileBuffer;
    Status = st->BootServices->AllocatePool(EfiLoaderData, FileSize, &FileBuffer);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Failed to allocate memory for file\r\n");
        return Status;
    }

    uintN_t ReadSize = FileSize;
    Status = File->Read(File, &ReadSize, FileBuffer);
    if (Status != EFI_SUCCESS || ReadSize != FileSize) {
        Print(st, u"Failed to read file\r\n");
        return Status;
    }

    File->Close(File);
    Root->Close(Root);

    *BufferOut = FileBuffer;
    *SizeOut = FileSize;
    return EFI_SUCCESS;
}


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


EFI_STATUS LoadMachOKernel(struct EFI_SYSTEM_TABLE* st, EFI_HANDLE ImageHandle, uint64_t* EntryPoint, uint64_t** OutPml4, chimera_boot_info_t* boot_info) {
    void* KernelBuffer = NULL;
    uintN_t KernelSize = 0;
    uint16_t KernelPath[] = { '\\', 'S', 'y', 's', 't', 'e', 'm', '\\', 'L', 'i', 'b', 'r', 'a', 'r', 'y', '\\', 'K', 'e', 'r', 'n', 'e', 'l', 's', '\\', 'm', 'a', 'c', 'h', '_', 'k', 'e', 'r', 'n', 'e', 'l', 0 };
    
    EFI_STATUS Status = ReadFile(st, ImageHandle, KernelPath, &KernelBuffer, &KernelSize);
    if (Status != EFI_SUCCESS) {
        Print(st, u"Could not read \\mach_kernel\r\n");
        return Status;
    }

    struct mach_header_64* header = (struct mach_header_64*)KernelBuffer;
    if (header->magic != MH_MAGIC_64) {
        Print(st, u"Not a valid 64-bit Mach-O kernel\r\n");
        return EFI_UNSUPPORTED;
    }

    // 1. Allocate PML4
    uint64_t* pml4 = AllocateZeroedPage(st);
    if (!pml4) {
        Print(st, u"Failed to allocate PML4\r\n");
        return EFI_UNSUPPORTED;
    }

    // 2. Map lowest 4GB identity (for UEFI and transition)
    for (uint64_t i = 0; i < 4 * 1024 * 1024 * 1024ULL; i += PAGE_2M_SIZE) {
        MapPage2M(st, pml4, i, i);
    }
    
    // 3. Map HHDM (Higher Half Direct Map) for first 4GB
    uint64_t hhdm_base = 0xffff800000000000ULL;
    for (uint64_t i = 0; i < 4 * 1024 * 1024 * 1024ULL; i += PAGE_2M_SIZE) {
        MapPage2M(st, pml4, hhdm_base + i, i);
    }

    uint8_t* cmd_ptr = (uint8_t*)KernelBuffer + sizeof(struct mach_header_64);
    uint64_t phys_start = (uint64_t)-1;
    uint64_t phys_end = 0;
    
    for (uint32_t i = 0; i < header->ncmds; i++) {
        struct load_command* lc = (struct load_command*)cmd_ptr;
        
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64* seg = (struct segment_command_64*)lc;
            
            // Ignore PAGEZERO
            if (seg->vmsize == 0 || seg->vmaddr < 0x1000) {
                cmd_ptr += lc->cmdsize;
                continue;
            }

            uintN_t Pages = EFI_SIZE_TO_PAGES(seg->vmsize);
            EFI_PHYSICAL_ADDRESS PhysAddr;
            Status = st->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, Pages, &PhysAddr);
            if (Status != EFI_SUCCESS) {
                Print(st, u"Failed to allocate pages for segment\r\n");
                return Status;
            }

            memset((void*)PhysAddr, 0, seg->vmsize);
            if (seg->filesize > 0) {
                memcpy((void*)PhysAddr, (uint8_t*)KernelBuffer + seg->fileoff, seg->filesize);
            }

            // Map the segment into our PML4
            for (uint64_t offset = 0; offset < seg->vmsize; offset += PAGE_SIZE) {
                MapPage(st, pml4, seg->vmaddr + offset, PhysAddr + offset);
            }
            
            if (PhysAddr < phys_start) phys_start = PhysAddr;
            if (PhysAddr + seg->vmsize > phys_end) phys_end = PhysAddr + seg->vmsize;
        } 
        else if (lc->cmd == LC_UNIXTHREAD) {
            struct thread_command* tc = (struct thread_command*)lc;
            *EntryPoint = tc->rip;
        }
        
        cmd_ptr += lc->cmdsize;
    }
    
    boot_info->kernel_phys_start = phys_start;
    boot_info->kernel_phys_end = phys_end;
    *OutPml4 = pml4;

    return EFI_SUCCESS;
}

static int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = s1;
    const unsigned char* p2 = s2;
    while(n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

// Global boot info
chimera_boot_info_t g_boot_info;

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, struct EFI_SYSTEM_TABLE *SystemTable) {
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    Print(SystemTable, u"XIU Mach-O UEFI Bootloader Stage 1\r\n");

    memset(&g_boot_info, 0, sizeof(g_boot_info));
    g_boot_info.magic = CHIMERA_BOOT_MAGIC;
    g_boot_info.version = 1;
    
    // Find ACPI RSDP
    EFI_GUID Acpi20TableGuid = {0x8868E871, 0xE4F1, 0x11D3, {0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81}};
    for (uintN_t i = 0; i < SystemTable->NumberOfTableEntries; i++) {
        if (memcmp(&SystemTable->ConfigurationTable[i].VendorGuid, &Acpi20TableGuid, sizeof(EFI_GUID)) == 0) {
            g_boot_info.rsdp_base = (uint64_t)SystemTable->ConfigurationTable[i].VendorTable;
            break;
        }
    }

    uint64_t EntryPoint = 0;
    uint64_t* Pml4 = NULL;
    
    EFI_STATUS Status = LoadMachOKernel(SystemTable, ImageHandle, &EntryPoint, &Pml4, &g_boot_info);
    if (Status != EFI_SUCCESS) {
        Print(SystemTable, u"Failed to load mach_kernel\r\n");
        while(1);
    }
    Print(SystemTable, u"Kernel loaded successfully!\r\n");

    // Get GOP
    EFI_GUID gopGuid = {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
    struct EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    Status = SystemTable->BootServices->LocateProtocol(&gopGuid, NULL, (void**)&gop);
    if (Status == EFI_SUCCESS && gop && gop->Mode) {
        g_boot_info.fb_base = gop->Mode->FrameBufferBase;
        if (gop->Mode->Info) {
            g_boot_info.fb_width = gop->Mode->Info->HorizontalResolution;
            g_boot_info.fb_height = gop->Mode->Info->VerticalResolution;
            g_boot_info.fb_pitch = gop->Mode->Info->PixelsPerScanLine * 4;
            g_boot_info.fb_bpp = 32;
        }
    } else {
        Print(SystemTable, u"Warning: GOP not found, no framebuffer!\r\n");
    }

    // Get Memory Map size
    uintN_t MapSize = 0;
    uintN_t MapKey = 0;
    uintN_t DescSize = 0;
    uint32_t DescVersion = 0;
    
    SystemTable->BootServices->GetMemoryMap(&MapSize, NULL, &MapKey, &DescSize, &DescVersion);
    MapSize += 4096 * 2; // Add some room for the allocations we are about to make
    
    void* MemMap = NULL;
    SystemTable->BootServices->AllocatePool(EfiLoaderData, MapSize, &MemMap);

    uintN_t MaxEntries = MapSize / DescSize;
    chimera_memmap_entry_t* TranslatedMap = NULL;
    SystemTable->BootServices->AllocatePool(EfiLoaderData, MaxEntries * sizeof(chimera_memmap_entry_t), (void**)&TranslatedMap);
    
    // Now get the actual memory map
    Status = SystemTable->BootServices->GetMemoryMap(&MapSize, MemMap, &MapKey, &DescSize, &DescVersion);
    if (Status != EFI_SUCCESS) {
        Print(SystemTable, u"Failed to get memory map (2nd time)\r\n");
        while(1);
    }

    uintN_t EntryCount = MapSize / DescSize;
    for (uintN_t i = 0; i < EntryCount; i++) {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)MemMap + (i * DescSize));
        TranslatedMap[i].base = desc->PhysicalStart;
        TranslatedMap[i].length = desc->NumberOfPages * 4096;
        
        switch (desc->Type) {
            case EfiConventionalMemory:
                TranslatedMap[i].type = CHIMERA_MEM_USABLE;
                break;
            case EfiLoaderCode:
            case EfiLoaderData:
                TranslatedMap[i].type = CHIMERA_MEM_KERNEL;
                break;
            case EfiBootServicesCode:
            case EfiBootServicesData:
                TranslatedMap[i].type = CHIMERA_MEM_BOOTLOADER_RECLAIM;
                break;
            case EfiACPIReclaimMemory:
                TranslatedMap[i].type = CHIMERA_MEM_ACPI_RECLAIM;
                break;
            case EfiACPIMemoryNVS:
                TranslatedMap[i].type = CHIMERA_MEM_ACPI_NVS;
                break;
            default:
                TranslatedMap[i].type = CHIMERA_MEM_RESERVED;
                break;
        }
    }
    
    g_boot_info.memmap_base = (uint64_t)TranslatedMap;
    g_boot_info.memmap_desc_size = sizeof(chimera_memmap_entry_t);
    g_boot_info.memmap_count = EntryCount;

    // Exit Boot Services
    Status = SystemTable->BootServices->ExitBootServices(ImageHandle, MapKey);
    if (Status != EFI_SUCCESS) {
        Print(SystemTable, u"ExitBootServices failed!\r\n");
        while(1);
    }

    // Switch page tables
    __asm__ volatile("mov %0, %%cr3" : : "r"(Pml4));

    // Jump to kernel using System V ABI (SysV passes first arg in %rdi, MS ABI uses %rcx)
    void __attribute__((sysv_abi)) (*KernelEntry)(chimera_boot_info_t*) = (void __attribute__((sysv_abi)) (*)(chimera_boot_info_t*))EntryPoint;
    KernelEntry(&g_boot_info);
    
    while(1); // Should never be reached
    return EFI_SUCCESS;
}

