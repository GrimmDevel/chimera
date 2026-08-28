#pragma once
#include <kernel/chimera_types.h>

struct idt_entry {
    u16 base_low;
    u16 selector;
    u8  ist;
    u8  flags;
    u16 base_mid;
    u32 base_high;
    u32 reserved;
} CHIMERA_PACKED;

struct idtr {
    u16 limit;
    u64 base;
} CHIMERA_PACKED;

#ifdef __cplusplus
extern "C" {
#endif

void idt_init(void);
void idt_reload(void);
void idt_set_gate(u8 vector, void *handler, u8 flags);

#ifdef __cplusplus
}
#endif
