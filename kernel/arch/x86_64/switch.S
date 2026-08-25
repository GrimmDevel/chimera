// context switch (x86_64)
.intel_syntax noprefix

.global context_switch
context_switch:
    // rdi = old_stack_ptr (void**), rsi = new_stack_ptr (void*), rdx = new_cr3, rcx = old_fp (void*), r8 = new_fp (void*)
    pushfq
    cli
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    // save old FP/SSE state if pointer provided
    test rcx, rcx
    jz 1f
    fxsave64 [rcx]
1:
    mov [rdi], rsp
    mov rsp, rsi

    // restore new FP/SSE state if pointer provided
    test r8, r8
    jz 2f
    fxrstor64 [r8]
2:
    // switch cr3 if needed
    mov rax, cr3
    cmp rax, rdx
    je 3f
    mov cr3, rdx
3:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    popfq

    ret
