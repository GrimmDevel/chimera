/*
 * XIU OS Objective-C 2.0 Message Dispatch and ABI Symbols for x86_64
 */

.text
.globl _objc_msgSend
.globl _objc_msgSendSuper
.globl _objc_msgSendSuper2
.globl _objc_msgSend_stret
.globl _objc_msgSend_fpret

.align 4
_objc_msgSend:
    testq %rdi, %rdi
    jz .Lnil_ret

    /* save all argument registers — ABI requires rdi,rsi,rdx,rcx,r8,r9 */
    pushq %rbp
    movq  %rsp, %rbp
    subq  $0x60, %rsp
    movq  %rdi, -0x08(%rbp)
    movq  %rsi, -0x10(%rbp)
    movq  %rdx, -0x18(%rbp)
    movq  %rcx, -0x20(%rbp)
    movq  %r8,  -0x28(%rbp)
    movq  %r9,  -0x30(%rbp)

    /* rdi=self, rsi=_cmd already set */
    call  _objc_msgSend_lookup

    movq  %rax, %r11

    movq  -0x08(%rbp), %rdi
    movq  -0x10(%rbp), %rsi
    movq  -0x18(%rbp), %rdx
    movq  -0x20(%rbp), %rcx
    movq  -0x28(%rbp), %r8
    movq  -0x30(%rbp), %r9

    movq  %rbp, %rsp
    popq  %rbp
    jmpq  *%r11

.Lnil_ret:
    xorq %rax, %rax
    retq

.align 4
_objc_msgSendSuper:
    testq %rdi, %rdi
    jz .Lnil_ret_super

    pushq %rbp
    movq  %rsp, %rbp
    subq  $0x60, %rsp
    movq  %rdi, -0x08(%rbp)
    movq  %rsi, -0x10(%rbp)
    movq  %rdx, -0x18(%rbp)
    movq  %rcx, -0x20(%rbp)
    movq  %r8,  -0x28(%rbp)
    movq  %r9,  -0x30(%rbp)

    /* rdi=struct objc_super*, rsi=_cmd */
    call  _objc_msgSendSuper_lookup

    movq  %rax, %r11

    movq  -0x08(%rbp), %rax       /* rax = struct objc_super* */
    movq  (%rax), %rdi          /* rdi = super->receiver */
    movq  -0x10(%rbp), %rsi
    movq  -0x18(%rbp), %rdx
    movq  -0x20(%rbp), %rcx
    movq  -0x28(%rbp), %r8
    movq  -0x30(%rbp), %r9

    movq  %rbp, %rsp
    popq  %rbp
    jmpq  *%r11

.Lnil_ret_super:
    xorq %rax, %rax
    retq

.align 4
_objc_msgSendSuper2:
    testq %rdi, %rdi
    jz .Lnil_ret_super2

    pushq %rbp
    movq  %rsp, %rbp
    subq  $0x60, %rsp
    movq  %rdi, -0x08(%rbp)
    movq  %rsi, -0x10(%rbp)
    movq  %rdx, -0x18(%rbp)
    movq  %rcx, -0x20(%rbp)
    movq  %r8,  -0x28(%rbp)
    movq  %r9,  -0x30(%rbp)

    /* rdi=struct objc_super*, rsi=_cmd */
    call  _objc_msgSendSuper2_lookup

    movq  %rax, %r11

    movq  -0x08(%rbp), %rax       /* rax = struct objc_super* */
    movq  (%rax), %rdi          /* rdi = super->receiver */
    movq  -0x10(%rbp), %rsi
    movq  -0x18(%rbp), %rdx
    movq  -0x20(%rbp), %rcx
    movq  -0x28(%rbp), %r8
    movq  -0x30(%rbp), %r9

    movq  %rbp, %rsp
    popq  %rbp
    jmpq  *%r11

.Lnil_ret_super2:
    xorq %rax, %rax
    retq

.align 4
_objc_msgSend_stret:
    /* stret: rdi=sret_ptr, rsi=self, rdx=_cmd */
    testq %rsi, %rsi
    jz .Lstret_nil

    pushq %rbp
    movq  %rsp, %rbp
    subq  $0x60, %rsp
    movq  %rdi, -0x08(%rbp)
    movq  %rsi, -0x10(%rbp)
    movq  %rdx, -0x18(%rbp)
    movq  %rcx, -0x20(%rbp)
    movq  %r8,  -0x28(%rbp)
    movq  %r9,  -0x30(%rbp)

    movq  %rsi, %rdi            /* self */
    movq  %rdx, %rsi            /* _cmd */
    call  _objc_msgSend_lookup

    movq  %rax, %r11
    movq  -0x08(%rbp), %rdi
    movq  -0x10(%rbp), %rsi
    movq  -0x18(%rbp), %rdx
    movq  -0x20(%rbp), %rcx
    movq  -0x28(%rbp), %r8
    movq  -0x30(%rbp), %r9
    movq  %rbp, %rsp
    popq  %rbp
    jmpq  *%r11

.Lstret_nil:
    retq

.align 4
_objc_msgSend_fpret:
    jmp _objc_msgSend

.data
.align 8
.globl __objc_empty_cache
__objc_empty_cache:
    .space 128

.globl ___CFConstantStringClassReference
___CFConstantStringClassReference:
    .space 128

.globl _objc_ehtype_vtable
_objc_ehtype_vtable:
    .space 128

