/* Exception vector tables.
 *
 * An AArch64 vector table has 16 entries, each 0x80 bytes (32 instructions),
 * and the whole table must be 2 KB aligned (VBAR_ELx bits [10:0] are RES0).
 * The 16 entries are four groups of four:
 *
 *   offset  taken from                              type
 *   0x000   current EL, using SP_EL0                Sync, IRQ, FIQ, SError (0x000/0x080/0x100/0x180)
 *   0x200   current EL, using SP_ELx                Sync, IRQ, FIQ, SError
 *   0x400   lower EL, AArch64                       Sync, IRQ, FIQ, SError
 *   0x600   lower EL, AArch32                       Sync, IRQ, FIQ, SError
 *
 * (RV32 comparison: mtvec points at one handler, or at a table of 4-byte
 * jumps in vectored mode, and mcause says why. Here the hardware picks the
 * entry from the source and the type, and ESR_ELx then gives the detail.)
 *
 * Each exception level has its own VBAR (VBAR_EL1/2/3) and its own ELR, SPSR,
 * ESR and FAR. The `.macro` below is instantiated once per level (1, 2, 3), so
 * the code reads the right registers without checking CurrentEL.
 *
 * Every entry does the same thing: carve a trap_frame off the stack, save
 * x0 and x1 (so there are two scratch registers), put the entry number in x0,
 * and branch to the common code, which saves everything else, calls C, and
 * restores. The frame layout is struct trap_frame in include/trap.h:
 *   x0..x30 at 0..240, ELR 248, SPSR 256, ESR 264, FAR 272, entry number 280.
 */
#define FRAME 288

    .macro vector_entry el, n
    .align 7                        /* 2^7 = 0x80 bytes per entry */
    sub     sp, sp, #FRAME          /* new frame on the stack that was in use */
    stp     x0, x1, [sp, #0]
    mov     x0, #\n                 /* which entry: 0..15 */
    b       exc_common_el\el
    .endm

    .macro exception_code el
exc_common_el\el:
    /* x0 = entry number, x1 is free (old values already in the frame). */
    stp     x2, x3,   [sp, #16]
    stp     x4, x5,   [sp, #32]
    stp     x6, x7,   [sp, #48]
    stp     x8, x9,   [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30,      [sp, #240]

    mrs     x1, elr_el\el
    mrs     x2, spsr_el\el
    mrs     x3, esr_el\el
    mrs     x4, far_el\el
    stp     x1, x2, [sp, #248]      /* ELR, SPSR */
    stp     x3, x4, [sp, #264]      /* ESR, FAR  */
    str     x0,     [sp, #280]      /* entry number */

    mov     x0, sp                  /* argument 1: the frame */
    mov     x1, #\el                /* argument 2: which EL we are in */
    bl      exception_handler

    /* The handler may have changed ELR (to skip an instruction), so write
     * the saved values back before returning. */
    ldp     x1, x2, [sp, #248]
    msr     elr_el\el, x1
    msr     spsr_el\el, x2

    ldp     x2, x3,   [sp, #16]
    ldp     x4, x5,   [sp, #32]
    ldp     x6, x7,   [sp, #48]
    ldp     x8, x9,   [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30,      [sp, #240]
    ldp     x0, x1,   [sp, #0]
    add     sp, sp, #FRAME
    eret                            /* PC <- ELR_ELx, PSTATE <- SPSR_ELx */
    .endm

    .macro vector_table el
    .balign 2048                    /* VBAR needs a 2 KB aligned table */
    .global vectors_el\el
vectors_el\el:
    vector_entry \el, 0             /* current EL, SP_EL0: Sync   */
    vector_entry \el, 1             /*                     IRQ    */
    vector_entry \el, 2             /*                     FIQ    */
    vector_entry \el, 3             /*                     SError */
    vector_entry \el, 4             /* current EL, SP_ELx: Sync   */
    vector_entry \el, 5
    vector_entry \el, 6
    vector_entry \el, 7
    vector_entry \el, 8             /* lower EL, AArch64: Sync    */
    vector_entry \el, 9
    vector_entry \el, 10
    vector_entry \el, 11
    vector_entry \el, 12            /* lower EL, AArch32: Sync    */
    vector_entry \el, 13
    vector_entry \el, 14
    vector_entry \el, 15
    exception_code \el
    .endm

    .section .text.vectors, "ax"
    vector_table 1
    vector_table 2
    vector_table 3
