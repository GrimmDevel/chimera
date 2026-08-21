/* =============================================================================
 * XIU Operating System — Apple XNU Mach VM Subsystem Interface
 * kernel/include/kernel/vm_xnu.h
 * Derived from XNU osfmk/mach/vm_map.h & vm_prot.h
 * ============================================================================= */

#ifndef XIU_VM_XNU_H
#define XIU_VM_XNU_H

#include <kernel/xiu_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef u32 vm_prot_t;

#define VM_PROT_NONE        ((vm_prot_t) 0x00)
#define VM_PROT_READ        ((vm_prot_t) 0x01)
#define VM_PROT_WRITE       ((vm_prot_t) 0x02)
#define VM_PROT_EXECUTE     ((vm_prot_t) 0x04)
#define VM_PROT_DEFAULT     (VM_PROT_READ | VM_PROT_WRITE)
#define VM_PROT_ALL         (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)

#define VM_FLAGS_ANYWHERE   0x0001
#define VM_FLAGS_FIXED      0x0000
#define VM_FLAGS_NO_CACHE   0x0010

typedef struct {
    u64 address;
    u64 size;
    vm_prot_t protection;
    vm_prot_t max_protection;
    u32 inheritance;
    bool shared;
    bool reserved;
} vm_region_basic_info_t;

#ifdef __cplusplus
}
#endif

#endif /* XIU_VM_XNU_H */
