// context switch (x86_64)
.intel_syntax noprefix

.global context_switch
context_switch:
    // rdi = old_stack_ptr (void**), rsi = new_stack_ptr (void*), rdx = new_cr3
    pushfq
    cli
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, rsi

    // switch cr3 if needed
    mov rax, cr3
    cmp rax, rdx
    je 1f
    mov cr3, rdx
1:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    popfq

    ret
