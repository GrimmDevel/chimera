/* linker script */
PHDRS
{
    text    PT_LOAD    FLAGS(5);
    rodata  PT_LOAD    FLAGS(4);
    data    PT_LOAD    FLAGS(6);
}

ENTRY(chimera_kernel_main)

SECTIONS
{
    . = 0xFFFFFFFF80000000;

    .text : {
        *(.text .text.*)
    } :text

    . += 0x1000;

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . += 0x1000;

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.interp)
        *(.comment)
        *(.note .note.*)
    }
}
