// c runtime entry point (darwin abi)
.text
.globl _start

_start:
    xorq %rbp, %rbp

    movq (%rsp), %rdi      // argc
    leaq 8(%rsp), %rsi     // argv
    
    // envp = argv + argc + 1
    movq %rdi, %rax
    incq %rax
    shlq $3, %rax
    leaq (%rsi, %rax), %rdx // envp

    movq %rdx, _environ(%rip)
    
    andq $-16, %rsp

    callq _main

    movq %rax, %rdi
    callq _exit

1:  jmp 1b

// generic syscall wrapper
.globl _chimera_syscall
_chimera_syscall:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    movq %r9, %r8
    movq 8(%rsp), %r9
    syscall
    retq

.globl ____chkstk_darwin
____chkstk_darwin:
    retq

.globl ___chkstk_darwin
___chkstk_darwin:
    retq

