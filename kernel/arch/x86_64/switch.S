// context switch (x86_64)
.intel_syntax noprefix

.extern _g_fpu_mask_lo
.extern _g_fpu_mask_hi

.global _context_switch
_context_switch:
    // rdi = old_stack_ptr (void**), rsi = new_stack_ptr (void*), rdx = new_cr3, rcx = old_fp (void*), r8 = new_fp (void*), r9 = old_running_cpu (u32*)
    pushfq
    cli
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    // preserve new_cr3: the xsave mask load uses edx, and rdx carries the
    // new CR3 argument. r12 is free here (old thread's r12 already saved;
    // the new thread's r12 comes back from its stack at the pops).
    mov r12, rdx

    // save old FP/SSE/AVX state if pointer provided (xsave honors XCR0)
    test rcx, rcx
    jz 1f
    mov eax, dword ptr [rip + _g_fpu_mask_lo]
    mov edx, dword ptr [rip + _g_fpu_mask_hi]
    xsave64 [rcx]
1:
    mov [rdi], rsp
    mov rsp, rsi

    // ponytail: release old thread's running_cpu ONLY after RSP is safely on
    // new stack, preventing another CPU from picking the old thread while this
    // core is still running on its stack.
    test r9, r9
    jz 2f
    mov DWORD PTR [r9], 0xFFFFFFFF
2:

    // restore new FP/SSE/AVX state if pointer provided
    test r8, r8
    jz 3f
    mov eax, dword ptr [rip + _g_fpu_mask_lo]
    mov edx, dword ptr [rip + _g_fpu_mask_hi]
    xrstor64 [r8]
3:
    // switch cr3 if needed
    mov rax, cr3
    cmp rax, r12
    je 4f
    mov cr3, r12
4:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    popfq
    // NOTE: NO sti here! The saved rflags correctly reflect the IF state
    // at the time of the switch. Adding sti would break spinlock protection
    // (a thread preempted inside a critical section would get interrupts
    // re-enabled, allowing another CPU to enter the same critical section).
    // If the thread was preempted from an ISR, IF=0 is temporary — the
    // thread resumes in the ISR which does iretq, restoring user IF=1.

    ret
