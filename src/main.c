#include "printf.h"
#include "sysreg.h"
#include "trap.h"

/* Read CurrentEL. The exception level is in bits [3:2]; bits [1:0] and the
 * rest read as zero. So the raw value is 0x4 * EL: EL1=0x4, EL2=0x8, EL3=0xc. */
static unsigned long read_currentel(void)
{
    unsigned long v;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(v));
    return v;
}

void main(void)
{
    unsigned long el = read_currentel();

    printf("AArch64 bare metal on QEMU virt\n");
    printf("CurrentEL = 0x%lx (EL%d)\n", el, (int)(el >> 2));

    /* Point VBAR_EL3 at the EL3 vector table. We are still in EL3 in this
     * step. The ISB makes sure the write has taken effect before we fault. */
    write_sysreg(vbar_el3, vectors_el3);
    isb();

    printf("\ntest 1: svc #0x42\n");
    /* Put known values in x9 and x28 and check they survive the exception:
     * this tests that the vector code saves and restores the registers. */
    unsigned long a = 0x1122334455667788UL, b = 0x99aabbccddeeff00UL;
    __asm__ volatile("mov x9, %0\n mov x28, %1\n svc #0x42\n mov %0, x9\n mov %1, x28"
                     : "+r"(a), "+r"(b) :: "x9", "x28");
    printf("back after svc; registers %s\n",
           (a == 0x1122334455667788UL && b == 0x99aabbccddeeff00UL) ? "preserved" : "CORRUPTED");

    printf("\ntest 2: undefined instruction\n");
    __asm__ volatile(".inst 0x00000000");   /* permanently undefined (UDF #0) */
    printf("back after undefined instruction\n");

    printf("\ntest 3: unaligned access\n");
    /* SCTLR_ELx.A (bit 1) turns on alignment checking for every data access:
     * an 8-byte load from an odd address then faults. (With the MMU off, the
     * architecture treats all data accesses as Device memory, which must be
     * aligned even with A clear; QEMU 8.2 does not enforce that, so we set A.) */
    write_sysreg(sctlr_el3, read_sysreg(sctlr_el3) | (1UL << 1));
    isb();
    unsigned long dummy;
    __asm__ volatile("ldr %0, [%1]" : "=r"(dummy) : "r"(0x40000001UL));
    printf("back after unaligned access\n");

    for (;;)
        __asm__ volatile("wfe");
}
