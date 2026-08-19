.section .text

.extern interrupt_handler

.macro INTERRUPT_STUB num, has_err=0
.global intr\num
intr\num:
    .if \has_err == 0
    pushq $0
    .endif
    pushq $\num
    jmp interrupt_common
.endm

interrupt_common:
    // check if we came from userspace (cs & 3 == 3)
    testq $3, 24(%rsp)
    jz 1f
    swapgs
1:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    cld
    call interrupt_handler

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    // check if returning to userspace
    testq $3, 24(%rsp)
    jz 2f
    // returning to userspace - swap GS back to user GS BASE
    swapgs
2:
    addq $16, %rsp
    iretq

// exceptions 0-31
INTERRUPT_STUB 0
INTERRUPT_STUB 1
INTERRUPT_STUB 2
INTERRUPT_STUB 3
INTERRUPT_STUB 4
INTERRUPT_STUB 5
INTERRUPT_STUB 6
INTERRUPT_STUB 7
INTERRUPT_STUB 8, 1
INTERRUPT_STUB 9
INTERRUPT_STUB 10, 1
INTERRUPT_STUB 11, 1
INTERRUPT_STUB 12, 1
INTERRUPT_STUB 13, 1
INTERRUPT_STUB 14, 1
INTERRUPT_STUB 15
INTERRUPT_STUB 16
INTERRUPT_STUB 17, 1
INTERRUPT_STUB 18
INTERRUPT_STUB 19
INTERRUPT_STUB 20
INTERRUPT_STUB 21
INTERRUPT_STUB 22
INTERRUPT_STUB 23
INTERRUPT_STUB 24
INTERRUPT_STUB 25
INTERRUPT_STUB 26
INTERRUPT_STUB 27
INTERRUPT_STUB 28
INTERRUPT_STUB 29
INTERRUPT_STUB 30, 1
INTERRUPT_STUB 31

// iRQs 32-47
INTERRUPT_STUB 32
INTERRUPT_STUB 33
INTERRUPT_STUB 34
INTERRUPT_STUB 35
INTERRUPT_STUB 36
INTERRUPT_STUB 37
INTERRUPT_STUB 38
INTERRUPT_STUB 39
INTERRUPT_STUB 40
INTERRUPT_STUB 41
INTERRUPT_STUB 42
INTERRUPT_STUB 43
INTERRUPT_STUB 44
INTERRUPT_STUB 45
INTERRUPT_STUB 46
INTERRUPT_STUB 47

.section .data
.global interrupt_handlers
interrupt_handlers:
    .quad intr0
    .quad intr1
    .quad intr2
    .quad intr3
    .quad intr4
    .quad intr5
    .quad intr6
    .quad intr7
    .quad intr8
    .quad intr9
    .quad intr10
    .quad intr11
    .quad intr12
    .quad intr13
    .quad intr14
    .quad intr15
    .quad intr16
    .quad intr17
    .quad intr18
    .quad intr19
    .quad intr20
    .quad intr21
    .quad intr22
    .quad intr23
    .quad intr24
    .quad intr25
    .quad intr26
    .quad intr27
    .quad intr28
    .quad intr29
    .quad intr30
    .quad intr31
    .quad intr32
    .quad intr33
    .quad intr34
    .quad intr35
    .quad intr36
    .quad intr37
    .quad intr38
    .quad intr39
    .quad intr40
    .quad intr41
    .quad intr42
    .quad intr43
    .quad intr44
    .quad intr45
    .quad intr46
    .quad intr47
    .rept 208
        .quad intr31
    .endr
