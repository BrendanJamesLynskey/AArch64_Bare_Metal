/* Entry point. QEMU starts every core at the ELF entry point. With
 * secure=on that is at EL3; this code does not care which EL it is in yet.
 *
 * Steps:
 *   1. park every core except core 0
 *   2. set the stack pointer
 *   3. zero the BSS
 *   4. call main()
 */
    .section .text.boot, "ax"
    .global _start
_start:
    /* 1. Which core am I? MPIDR_EL1 affinity fields Aff0..Aff2 hold the core
     * number on QEMU virt (Aff0 = core id). Mask them and park anything non-zero.
     * RV32 comparison: this is reading mhartid and parking harts != 0. */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xffffff
    cbnz    x0, park

    /* 2. Stack. SP must be 16-byte aligned whenever it is used for a memory
     * access (hardware checks this when SP is the base register). The
     * linker script places __stack_top on a 16-byte boundary. */
    ldr     x0, =__stack_top
    mov     sp, x0

    /* 3. Zero the BSS, 16 bytes at a time (both ends are 16-aligned). */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
1:  cmp     x0, x1
    b.hs    2f
    stp     xzr, xzr, [x0], #16
    b       1b

    /* 4. Into C. If main ever returns, fall into the park loop. */
2:  bl      main

park:
    wfe                         /* wait for event: low-power idle */
    b       park
