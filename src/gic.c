/* GICv3 setup. Three blocks of hardware take part:
 *
 *   distributor (GICD)    one per system: global enable, shared interrupts (SPIs)
 *   redistributor (GICR)  one per core: that core's private interrupts (SGIs 0-15,
 *                         PPIs 16-31) and its wake-up state
 *   CPU interface (ICC)   one per core: accessed with SYSTEM REGISTERS (ICC_*_EL1),
 *                         not memory-mapped: acknowledge, end of interrupt, priority mask
 *
 * The generic timer's interrupt is a PPI, so only the redistributor and CPU
 * interface matter for it; the distributor just has to be enabled.
 */
#include "gic.h"
#include "sysreg.h"

#define REG32(addr) (*(volatile unsigned int *)(addr))

/* Distributor registers */
#define GICD_CTLR        (GICD_BASE + 0x0000)
#define GICD_CTLR_ENGRP1 (1u << 1)    /* enable Group 1 interrupts (in the single security state) */
#define GICD_CTLR_ARE    (1u << 4)    /* affinity routing: required for the system-register interface */
#define GICD_CTLR_DS     (1u << 6)    /* disable security: one security state, EL1 can program everything */
#define GICD_CTLR_RWP    (1u << 31)   /* register write pending: wait for 0 */

/* Redistributor: two 64 KB frames per core. RD_base holds control/wake-up,
 * SGI_base (= RD_base + 0x10000) holds the SGI/PPI registers. */
#define GICR_WAKER(rd)        ((rd) + 0x0014)
#define GICR_WAKER_SLEEP      (1u << 1)   /* ProcessorSleep */
#define GICR_WAKER_CHILDASLP  (1u << 2)   /* ChildrenAsleep */
#define SGI_BASE(rd)          ((rd) + 0x10000)
#define GICR_IGROUPR0(sgi)    ((sgi) + 0x0080)
#define GICR_ISENABLER0(sgi)  ((sgi) + 0x0100)
#define GICR_IPRIORITYR(sgi)  ((sgi) + 0x0400)

/* Core 0's redistributor frame. (Other cores are 0x20000 apart.) */
#define RD0 GICR_BASE

/* ICC_SRE_ELx: bit 0 SRE (use the system register interface), bit 1 DFB,
 * bit 2 DIB (disable the legacy bypass), bit 3 Enable (let the next lower EL set SRE). */
#define ICC_SRE_ALL 0xfUL

void gic_el3_setup(void)
{
    /* Distributor: DS first (one security state), then affinity routing and Group 1. */
    REG32(GICD_CTLR) = GICD_CTLR_DS;
    while (REG32(GICD_CTLR) & GICD_CTLR_RWP)
        ;
    REG32(GICD_CTLR) = GICD_CTLR_DS | GICD_CTLR_ARE | GICD_CTLR_ENGRP1;
    while (REG32(GICD_CTLR) & GICD_CTLR_RWP)
        ;

    /* Allow EL2 and EL1 to use the system register interface. Each level's
     * SRE can only be set if the level above has Enable set. */
    write_sysreg(icc_sre_el3, ICC_SRE_ALL);
    isb();
    write_sysreg(icc_sre_el2, ICC_SRE_ALL);
    isb();

    /* CNTHCTL_EL2: bit 0 EL1PCTEN, bit 1 EL1PCEN. Without them, EL1 accesses
     * to the physical counter and timer registers trap to EL2. */
    write_sysreg(cnthctl_el2, 3);
    isb();
}

void gic_cpu_init(void)
{
    /* Redistributor: wake it. Clear ProcessorSleep, wait until ChildrenAsleep reads 0. */
    REG32(GICR_WAKER(RD0)) &= ~GICR_WAKER_SLEEP;
    while (REG32(GICR_WAKER(RD0)) & GICR_WAKER_CHILDASLP)
        ;

    /* CPU interface, at EL1: use system registers, accept every priority,
     * and enable Group 1 interrupts. */
    write_sysreg(icc_sre_el1, read_sysreg(icc_sre_el1) | 1UL);
    isb();
    write_sysreg(icc_pmr_el1, 0xff);      /* priority mask: signal anything with priority < 0xff */
    write_sysreg(icc_bpr1_el1, 0);
    write_sysreg(icc_igrpen1_el1, 1);
    isb();
}

/* Enable a private interrupt (INTID 0-31) on this core. */
void gic_enable_intid(unsigned intid)
{
    unsigned long sgi = SGI_BASE(RD0);

    REG32(GICR_IGROUPR0(sgi)) |= 1u << intid;                 /* Group 1 */
    *(volatile unsigned char *)(GICR_IPRIORITYR(sgi) + intid) = 0x80;   /* middle priority */
    REG32(GICR_ISENABLER0(sgi)) = 1u << intid;                /* write-1-to-set */
}
