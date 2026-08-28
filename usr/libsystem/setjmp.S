/* =============================================================================
 * Chimera Operating System — setjmp/longjmp (Mach-O Darwin ABI)
 * usr/libsystem/setjmp.S
 * ============================================================================= */

.text
.globl _setjmp
.globl __setjmp
.globl ___setjmp
.globl _sigsetjmp
.globl __sigsetjmp
_setjmp:
__setjmp:
___setjmp:
_sigsetjmp:
__sigsetjmp:
    movq %rbx, (0*8)(%rdi)
    movq %rbp, (1*8)(%rdi)
    movq %r12, (2*8)(%rdi)
    movq %r13, (3*8)(%rdi)
    movq %r14, (4*8)(%rdi)
    movq %r15, (5*8)(%rdi)
    
    // save stack pointer as it was BEFORE the call
    leaq 8(%rsp), %rax
    movq %rax, (6*8)(%rdi)
    
    // save return address
    movq (%rsp), %rax
    movq %rax, (7*8)(%rdi)
    
    xorq %rax, %rax
    retq

.globl _longjmp
.globl __longjmp
.globl ___longjmp
.globl _siglongjmp
.globl __siglongjmp
_longjmp:
__longjmp:
___longjmp:
_siglongjmp:
__siglongjmp:
    movq %rsi, %rax
    testq %rax, %rax
    jnz 1f
    incq %rax
1:
    movq (0*8)(%rdi), %rbx
    movq (1*8)(%rdi), %rbp
    movq (2*8)(%rdi), %r12
    movq (3*8)(%rdi), %r13
    movq (4*8)(%rdi), %r14
    movq (5*8)(%rdi), %r15
    movq (6*8)(%rdi), %rsp
    jmpq *(7*8)(%rdi)
