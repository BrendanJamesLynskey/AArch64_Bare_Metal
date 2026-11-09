/* The EL1 physical timer, from the Generic Timer, and the IRQ handler.
 *
 * Each core has a free-running 64-bit counter (CNTPCT_EL0) ticking at CNTFRQ_EL0
 * (62.5 MHz on QEMU virt), and a comparator per timer. CNTP_TVAL_EL0 is a
 * 32-bit down-counter view of the comparator: write N, and the timer fires N
 * counts from now. CNTP_CTL_EL0: bit 0 ENABLE, bit 1 IMASK (1 = do not
 * signal), bit 2 ISTATUS (condition met; read only).
 * The timer output is wired to PPI 14, INTID 30.
 * (RV32 comparison: the CLINT's mtime/mtimecmp. Here the compare is per core
 * and the interrupt goes through the GIC rather than being a core-local pin.)
 */
#include "gic.h"
#include "printf.h"
#include "sysreg.h"

static unsigned long interval;      /* counts per period */
static volatile unsigned long ticks;
static unsigned long t_start;

void timer_start(unsigned ms)
{
    unsigned long freq = read_sysreg(cntfrq_el0);
    interval = freq / 1000 * ms;
    printf("CNTFRQ_EL0 = %ld Hz, period %d ms = %ld counts\n", freq, (int)ms, interval);

    t_start = read_sysreg(cntpct_el0);
    write_sysreg(cntp_tval_el0, interval);
    write_sysreg(cntp_ctl_el0, 1);          /* ENABLE=1, IMASK=0 */
    isb();
}

void timer_rearm(void)
{
    write_sysreg(cntp_tval_el0, interval);  /* also clears ISTATUS: the new compare is in the future */
}

void timer_stop(void)
{
    write_sysreg(cntp_ctl_el0, 0);
}

unsigned long timer_ticks(void)
{
    return ticks;
}

/* Called from the exception code for an IRQ. */
void irq_handler(void)
{
    /* Acknowledge: reading ICC_IAR1_EL1 returns the INTID of the highest
     * priority pending Group 1 interrupt and makes it Active. 1023 means
     * "nothing pending" (spurious). */
    unsigned long intid = read_sysreg(icc_iar1_el1) & 0xffffff;

    if (intid == INTID_TIMER_PHYS) {
        ticks++;
        unsigned long now = read_sysreg(cntpct_el0);
        printf("tick %ld (%ld counts since start, period is %ld)\n", ticks, now - t_start, interval);
        timer_rearm();
    } else if (intid != 1023) {
        printf("unexpected INTID %ld\n", intid);
    }

    /* End of interrupt: tells the GIC we are done, so it can deliver the next
     * (priority drop and deactivation together, since ICC_CTLR_EL1.EOImode = 0). */
    if (intid != 1023)
        write_sysreg(icc_eoir1_el1, intid);
}
