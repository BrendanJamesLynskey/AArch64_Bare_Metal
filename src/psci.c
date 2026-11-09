/* A tiny PSCI (Power State Coordination Interface) service, to end the program.
 *
 * PSCI is the standard way for an OS to ask firmware for power control. The caller
 * puts a function ID in w0 and arguments in x1..x3 and executes SMC (to EL3)
 * or HVC (to EL2); the result comes back in x0.
 *
 * Because this program boots at EL3, QEMU does not provide PSCI: *we* are the
 * firmware. So EL3 has to handle the SMC itself. The EL1 side (psci_system_off)
 * makes the call; the EL3 side (psci_el3_handle, called from the exception
 * code when EL3 takes an SMC) implements two functions:
 *   PSCI_VERSION    returns 1.0
 *   SYSTEM_OFF      powers the machine off
 * Anything else returns NOT_SUPPORTED (-1).
 *
 * To power off, EL3 uses what QEMU `virt` provides when secure=on: a Secure-only
 * PL061 GPIO at 0x090b0000 whose output pin 0 is wired to "power down". Setting
 * the pin as an output and driving it high makes QEMU exit with status 0.
 */
#include "printf.h"
#include "trap.h"

#define PSCI_VERSION    0x84000000UL      /* 32-bit SMC calling convention function IDs */
#define PSCI_SYSTEM_OFF 0x84000008UL
#define PSCI_NOT_SUPPORTED (-1L)

#define SECURE_GPIO     0x090b0000UL
#define GPIODIR         (*(volatile unsigned int *)(SECURE_GPIO + 0x400))
#define GPIODATA_PIN0   (*(volatile unsigned int *)(SECURE_GPIO + (1u << 2)))   /* address bits [9:2] mask the pins: pin 0 */

/* EL1 side: ask EL3 to power off. */
static unsigned long smc(unsigned long fn)
{
    register unsigned long r0 __asm__("x0") = fn;
    __asm__ volatile("smc #0" : "+r"(r0) :: "x1", "x2", "x3", "memory");
    return r0;
}

void psci_system_off(void)
{
    printf("PSCI VERSION = 0x%lx\n", smc(PSCI_VERSION));
    printf("PSCI SYSTEM_OFF\n");
    smc(PSCI_SYSTEM_OFF);

    /* Not reached if the call worked. */
    printf("PSCI SYSTEM_OFF returned\n");
    for (;;)
        __asm__ volatile("wfe");
}

/* EL3 side. f->x[0] is the function ID; set f->x[0] to the result.
 * ELR_EL3 already points after the smc, so returning continues the caller. */
void psci_el3_handle(struct trap_frame *f)
{
    switch (f->x[0]) {
    case PSCI_VERSION:
        f->x[0] = 0x00010000;               /* major 1, minor 0 */
        break;
    case PSCI_SYSTEM_OFF:
        GPIODIR = 1;                        /* pin 0 is an output */
        GPIODATA_PIN0 = 1;                  /* drive it high: QEMU powers off */
        for (;;)                            /* not reached */
            __asm__ volatile("wfe");
    default:
        f->x[0] = (unsigned long)PSCI_NOT_SUPPORTED;
        break;
    }
}
