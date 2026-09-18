// x86_64 syscall entry and iretq switchers
.intel_syntax noprefix

#define CPU_LOCAL_CURRENT_THREAD    0x0
#define CPU_LOCAL_USER_RSP_SAVE     0x8
#define THREAD_KERNEL_STACK_OFFSET  0x30

.global _x86_64_syscall_entry
.extern _syscall_dispatch
.extern _sys_exit_direct

.text
.align 16

_x86_64_syscall_entry:
    swapgs

    mov QWORD PTR gs:[CPU_LOCAL_USER_RSP_SAVE], rsp

    mov rsp, QWORD PTR gs:[CPU_LOCAL_CURRENT_THREAD]
    mov rsp, QWORD PTR [rsp + THREAD_KERNEL_STACK_OFFSET]

    push QWORD PTR gs:[CPU_LOCAL_USER_RSP_SAVE]
    push r11
    push rcx
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, r9
    mov r9, r8
    mov r8, r10
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax

    mov rbp, rsp
    and rsp, -16
    sub rsp, 16
    mov [rsp], r12
    mov [rsp + 8], rbp

    call _syscall_dispatch

    mov rsp, rbp

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    // sysret hardening: RCX is the user return RIP and the third slot still
    // on the stack holds the user RSP. SYSRET with a non-canonical RCX
    // raises #GP in ring 0 (Xen XSA-206 class), so both must be
    // user-canonical (< 2^47, i.e. high dword < 0x8000). The check reads the
    // stack slots directly and clobbers no GPR: RAX still carries the
    // syscall return value at this point.
    cmp     DWORD PTR [rsp+4], 0x8000       // user RIP (rcx slot)
    jae     9f
    cmp     DWORD PTR [rsp+20], 0x8000      // user RSP (3rd slot: rcx,r11,rsp)
    jae     9f
    pop rcx
    pop r11

    pop r10
    mov QWORD PTR gs:[CPU_LOCAL_USER_RSP_SAVE], r10
    mov rsp, QWORD PTR gs:[CPU_LOCAL_USER_RSP_SAVE]

    swapgs
    sysretq

9:  mov     rdi, 139
    call    _sys_exit_direct                // noreturn: kill caller (SIGSEGV)

.global _task_switch_to_user
_task_switch_to_user:
    cli

    mov ax, 0x1B
    mov ds, ax
    mov es, ax
    mov fs, ax

    push 0x1B
    push rsi
    push 0x202
    push 0x23
    push rdi

    swapgs
    iretq

.global _task_switch_to_user_frame
_task_switch_to_user_frame:
    cli
    
    mov r10, rdi
    mov r11, rsi
    mov rax, rcx

    test rdx, rdx
    jz 1f
    mov r15, [rdx + 0]
    mov r14, [rdx + 8]
    mov r13, [rdx + 16]
    mov r12, [rdx + 24]
    mov rbx, [rdx + 32]
    mov rbp, [rdx + 40]
1:

    mov dx, 0x1B
    mov ds, dx
    mov es, dx
    mov fs, dx

    push 0x1B
    push r11
    push 0x202
    push 0x23
    push r10

    swapgs
    iretq
