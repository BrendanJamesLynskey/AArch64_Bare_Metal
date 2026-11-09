/* Linker script for QEMU `virt`.
 *
 * Memory map of the machine (the parts that matter here):
 *   0x00000000  flash (64 MB x 2, unused: -kernel does not use it)
 *   0x08000000  GICv3 distributor    0x080A0000  GICv3 redistributors
 *   0x09000000  PL011 UART0
 *   0x40000000  start of RAM (default 128 MB; -m changes it)
 *
 * QEMU loads an ELF -kernel at the addresses in its program headers, so we
 * link the image at the start of RAM and the entry point is _start.
 */
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 128M
}

SECTIONS
{
    . = ORIGIN(RAM);

    /* .text.boot first, so _start is the very first byte of the image. */
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    /* Vector tables need 2 KB alignment; vectors.S aligns them itself. */
    .text.vectors : {
        *(.text.vectors)
    }

    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }

    /* BSS is zeroed by start.S, between __bss_start and __bss_end. */
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    /* Stacks: 16 KB, 16-byte aligned (SP must be 16-byte aligned).
     * Only core 0 runs C code for now, so there is just one. */
    . = ALIGN(4096);
    . += 16K;
    __stack_top = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
