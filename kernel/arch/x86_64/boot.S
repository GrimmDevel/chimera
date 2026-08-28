// pvh boot entry
.section .note.pvh.entry, "a"
.align 4
.long 4
.long 4
.long 18
.ascii "Xen\0"
.long _start

.section .bootstrap_stack, "aw", @nobits
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    cli

    mov $stack_top, %rsp

    push %rbx
    push %rax

    call chimera_kernel_main

1:
    hlt
    jmp 1b
