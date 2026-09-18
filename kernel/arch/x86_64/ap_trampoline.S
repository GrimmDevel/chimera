// AP startup trampoline — 16-bit real → 32-bit protected → 64-bit long mode
// kernel/arch/x86_64/ap_trampoline.S
//
// Copied by the BSP to physical 0x7000. APs start here via SIPI at
// CS:IP = 0x0700:0000 (16-bit real mode). The code transitions to long mode
// through 32-bit protected mode, loads per-AP stack/GS/C-entry from a data
// area, and jumps to the 64-bit C entry (smp_ap_entry_64).
//
// Memory layout (physical, all within the first 1MB):
//   0x7000: 16-bit code (this file, offset 0)
//   0x7100: 32-bit code (offset 0x100)
//   0x7200: 64-bit code (offset 0x200)
//   0x7300: GDT (5 entries × 8B = 40B)
//   0x7330: GDT descriptor (limit:word + base:qword)
//   0x7340: data: ap_stack_top (qword)
//   0x7348: data: ap_cpu_local (qword) — passed as RDI to C entry
//   0x7350: data: ap_c_entry (qword) — 64-bit C function
//   0x7358: data: ap_alive (qword) — set to 1 by the AP
//   0x8000: identity PML4 (BSP-built)
//   0x8000 + 4096: identity PDPT (BSP-built)
//
// Selectors: 0x08=code32, 0x10=data, 0x18=code64
// All far jumps use manual byte encoding for exact control.

.intel_syntax noprefix

.code16
.global _ap_tramp_start
_ap_tramp_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x6E00            // stack below trampoline

    // Load GDT: descriptor at physical 0x7330
    lgdt [0x7330]

    // Enable protected mode (CR0.PE, bit 0)
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    // Far jump: CS = 0x08 (code32), EIP = 0x7100
    // EA <word offset> <word CS> in 16-bit encoding
    .byte 0xEA
    .word 0x7100
    .word 0x0008

// ── 32-bit protected mode at linear 0x7100 ──
.code32
.org 0x100
prot_entry:
    mov ax, 0x10              // data selector
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    // Enable PAE (CR4.PAE, bit 5)
    mov eax, cr4
    or  eax, 0x20
    mov cr4, eax

    // Load identity PML4 (BSP-built at 0x8000)
    mov eax, 0x8000
    mov cr3, eax

    // Enable long mode: EFER.LME (MSR 0xC0000080, bit 8)
    mov ecx, 0xC0000080
    rdmsr
    or  eax, 0x100
    wrmsr

    // Enable paging (CR0.PG) — activates long mode
    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax

    // Far jump to 64-bit: CS = 0x18 (code64), EIP = 0x7200
    // EA <dword offset> <word CS> in 32-bit encoding
    .byte 0xEA
    .long 0x7200
    .word 0x0018

// ── 64-bit long mode at linear 0x7200 ──
.code64
.org 0x200
long_entry:
    mov ax, 0x10              // data selector
    mov ds, ax
    mov es, ax
    mov ss, ax

    // Load per-AP stack from data area (value only — used after CR3 switch)
    mov rsp, qword ptr [0x7340]

    // Set GS base to the per-CPU data (MSR_GS_BASE = 0xC0000101)
    // MSR is not affected by CR3 — safe to set before the switch
    mov rax, qword ptr [0x7348]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000101
    wrmsr

    // Switch to the kernel's CR3 so higher-half text/HHDM are mapped
    mov rax, qword ptr [0x7360]
    mov cr3, rax

    // NOW the kernel higher-half is mapped — safe to access C data
    mov rdi, qword ptr [0x7348]

    // Fetch the C entry before publishing readiness. The BSP reuses this
    // shared trampoline data page for the next AP after observing ap_alive.
    mov rax, qword ptr [0x7350]

    // Signal alive only after every shared datum has been consumed.
    mov qword ptr [0x7358], 1

    // Jump to the 64-bit C entry point (higher-half, now mapped).
    jmp rax

    // never returns

// ── GDT at offset 0x300 (physical 0x7300) ──
.org 0x300
tramp_gdt:
    .quad 0                       // null
    .quad 0x00CF9A000000FFFF      // 0x08: code32 — L=0, DB=1, G=1, RX
    .quad 0x00CF92000000FFFF      // 0x10: data — G=1, W
    .quad 0x00209A000000FFFF      // 0x18: code64 — L=1, G=1, RX

// ── GDT descriptor at offset 0x330 (physical 0x7330) ──
.org 0x330
tramp_gdt_desc:
    .word (4 * 8) - 1             // limit = 31
    .quad 0x0000000000007300      // base = 0x7300

// ── data area at offset 0x340 (physical 0x7340) ──
.org 0x340
ap_stack_top:
    .quad 0
.org 0x348
ap_cpu_local:
    .quad 0
.org 0x350
ap_c_entry:
    .quad 0
.org 0x358
ap_alive:
    .quad 0
.org 0x360
ap_kernel_cr3:
    .quad 0

// ── padding to the end of the trampoline page ──
.org 0xFFF
.byte 0
.global _ap_tramp_end
_ap_tramp_end:
