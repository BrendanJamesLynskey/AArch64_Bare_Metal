#include "printf.h"
#include "sysreg.h"
#include "trap.h"
#include "mmu.h"
#include "uart.h"

void drop_to_el1(void (*entry)(void));      /* el_drop.S */

/* Read CurrentEL. The exception level is in bits [3:2]; bits [1:0] and the
 * rest read as zero. So the raw value is 0x4 * EL: EL1=0x4, EL2=0x8, EL3=0xc. */
static unsigned long read_currentel(void)
{
    return read_sysreg(CurrentEL);
}

static void print_el(void)
{
    unsigned long el = read_currentel();
    printf("CurrentEL = 0x%lx (EL%d)\n", el, (int)(el >> 2));
}

/* Three deliberate exceptions, at whatever EL we are in. The handler (in
 * exception.c) prints ESR, ELR and FAR and returns past the faulting
 * instruction, so each test ends with a "back after" line. */
static void run_exception_tests(unsigned long el)
{
    printf("\ntest: svc #0x42\n");
    /* Put known values in x9 and x28 and check they survive the exception:
     * this tests that the vector code saves and restores the registers. */
    unsigned long a = 0x1122334455667788UL, b = 0x99aabbccddeeff00UL;
    __asm__ volatile("mov x9, %0\n mov x28, %1\n svc #0x42\n mov %0, x9\n mov %1, x28"
                     : "+r"(a), "+r"(b) :: "x9", "x28");
    printf("back after svc; registers %s\n",
           (a == 0x1122334455667788UL && b == 0x99aabbccddeeff00UL) ? "preserved" : "CORRUPTED");

    printf("\ntest: undefined instruction\n");
    __asm__ volatile(".inst 0x00000000");   /* permanently undefined (UDF #0) */
    printf("back after undefined instruction\n");

    printf("\ntest: unaligned access\n");
    /* SCTLR_ELx.A (bit 1) turns on alignment checking for every data access:
     * an 8-byte load from an odd address then faults. (With the MMU off, the
     * architecture treats all data accesses as Device memory, which must be
     * aligned even with A clear; QEMU 8.2 does not enforce that, so we set A.) */
    unsigned long dummy;
    if (el == 3) {
        write_sysreg(sctlr_el3, read_sysreg(sctlr_el3) | (1UL << 1));
    } else {
        write_sysreg(sctlr_el1, read_sysreg(sctlr_el1) | (1UL << 1));
    }
    isb();
    __asm__ volatile("ldr %0, [%1]" : "=r"(dummy) : "r"(0x40000001UL));
    printf("back after unaligned access\n");
}

/* This runs at EL1, entered by drop_to_el1(). The stack, VBAR_EL1 and
 * SCTLR_EL1 were set up on the way down. */
static void el1_main(void)
{
    printf("\n--- now in EL1 ---\n");
    print_el();
    run_exception_tests(1);

    printf("\n--- MMU ---\n");
    printf("SCTLR_EL1 before = 0x%lx\n", read_sysreg(sctlr_el1));
    mmu_init();
    unsigned long sctlr = read_sysreg(sctlr_el1);
    printf("SCTLR_EL1 after  = 0x%lx (M=%d C=%d I=%d)\n", sctlr, (int)(sctlr & 1),
           (int)((sctlr >> 2) & 1), (int)((sctlr >> 12) & 1));

    /* Ask the hardware to translate the alias VA, using the AT instruction
     * (address translation, stage 1, EL1, read). The result is in PAR_EL1:
     * bit 0 = 1 means the translation failed; otherwise bits [47:12] are the
     * physical page address. */
    __asm__ volatile("at s1e1r, %0" :: "r"(UART_ALIAS_VA));
    isb();
    unsigned long par = read_sysreg(par_el1);
    printf("AT of VA 0x%lx: PAR_EL1 = 0x%lx -> PA 0x%lx\n", UART_ALIAS_VA, par, par & 0xfffffffff000UL);

    /* The proof: print through a virtual address that only exists because
     * of the page tables. With the MMU off, 0x80000000 is a physical address
     * with nothing behind it. */
    uart_set_base(UART_ALIAS_VA);
    printf("hello through the second VA 0x%lx\n", UART_ALIAS_VA);
    uart_set_base(0x09000000UL);
    printf("and back through the identity mapping\n");

    /* An address with no mapping: a translation fault. The handler skips the load. */
    printf("\ntest: load from unmapped VA 0x100000000\n");
    unsigned long dummy;
    __asm__ volatile("ldr %0, [%1]" : "=r"(dummy) : "r"(0x100000000UL));
    printf("back after translation fault\n");

    for (;;)
        __asm__ volatile("wfe");
}

void main(void)
{
    printf("AArch64 bare metal on QEMU virt\n");
    print_el();

    /* VBAR_EL3 points at the EL3 vector table. ISB: make sure the write has
     * taken effect before anything can fault. */
    write_sysreg(vbar_el3, vectors_el3);
    isb();

    printf("\n--- exception tests at EL3 ---\n");
    run_exception_tests(3);

    printf("\n--- dropping EL3 -> EL2 -> EL1 ---\n");
    drop_to_el1(el1_main);
}
