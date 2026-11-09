/* void drop_to_el1(void (*entry)(void))  -- never returns.
 *
 * Called at EL3. Goes EL3 -> EL2 -> EL1 and continues at `entry` in EL1h.
 *
 * You cannot lower the exception level by writing a register. The only way
 * down is `eret`: it loads PSTATE from SPSR_ELx and the PC from ELR_ELx, so
 * we fake an exception return to a level below the current one. Set up SPSR
 * and ELR to say "return to EL2h at label 1", `eret`, and then do the same
 * trick again from EL2.
 * (RV32 comparison: the same idea as setting mstatus.MPP and mepc and
 * executing mret to enter S-mode or U-mode.)
 */
    .section .text, "ax"
    .global drop_to_el1
drop_to_el1:
    mov     x19, x0                 /* keep the entry point; x19 is not touched below */

    /* ---- at EL3 ---- */

    /* SCR_EL3, the Secure Configuration Register:
     *   bit 0  NS  = 1  the level below runs in Non-secure state
     *   bit 8  HCE = 1  the HVC instruction is enabled (else it is undefined)
     *   bit 10 RW  = 1  the next level down (EL2) is AArch64, not AArch32
     * Every other bit is 0: IRQ/FIQ/SError are not routed to EL3 and SMC is allowed. */
    mov     x1, #((1 << 10) | (1 << 8) | (1 << 0))
    msr     scr_el3, x1

    /* SPSR_EL3 is the PSTATE the eret will load:
     *   M[3:0] = 0b1001  EL2h (EL2, using SP_EL2)
     *   D, A, I, F (bits 9:6) = 1111  all exceptions masked for now */
    mov     x1, #0x3c9
    msr     spsr_el3, x1

    adr     x1, el2_entry           /* ELR_EL3 = where to continue */
    msr     elr_el3, x1
    eret

    /* ---- at EL2 ---- */
el2_entry:
    /* HCR_EL2, the Hypervisor Configuration Register:
     *   bit 31 RW = 1  EL1 is AArch64. All other bits stay 0, so there is no
     *   stage-2 translation (bit 0 VM=0) and no routing of exceptions to EL2
     *   (IMO/FMO/AMO = 0): EL1 handles its own IRQs, as it would without a hypervisor. */
    mov     x1, #(1 << 31)
    msr     hcr_el2, x1

    /* EL2 has its own vector table in case something goes wrong here. */
    ldr     x1, =vectors_el2
    msr     vbar_el2, x1

    /* SCTLR_EL1: write the known state "MMU off, caches off, alignment check
     * off", with the bits the architecture says must be 1 (RES1: 29, 28, 23,
     * 22, 20 and 11). Not relying on the reset value is safer. */
    ldr     x1, =0x30d00800
    msr     sctlr_el1, x1

    /* The EL1 stack, and the EL1 vectors. SP_EL1 is written from EL2 (EL1
     * cannot use the stack before it has one: the first exception would need it). */
    ldr     x1, =__stack_top
    msr     sp_el1, x1
    ldr     x1, =vectors_el1
    msr     vbar_el1, x1

    /* SPSR_EL2: M = 0b0101 is EL1h (EL1 using SP_EL1), DAIF masked. */
    mov     x1, #0x3c5
    msr     spsr_el2, x1
    msr     elr_el2, x19            /* the entry point we were given */
    eret
