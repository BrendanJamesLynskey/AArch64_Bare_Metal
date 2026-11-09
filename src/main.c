#include "printf.h"

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

    for (;;)
        __asm__ volatile("wfe");
}
