#ifndef GIC_H
#define GIC_H

/* GICv3 on QEMU `virt` (gic-version=3). */
#define GICD_BASE 0x08000000UL      /* distributor */
#define GICR_BASE 0x080A0000UL      /* redistributor frames, 128 KB per core; core 0 first */

#define INTID_TIMER_PHYS 30         /* the EL1 (Non-secure) physical timer: PPI 14 = INTID 16 + 14 */

void gic_el3_setup(void);           /* once, at EL3 */
void gic_cpu_init(void);            /* redistributor and CPU interface of this core, at EL1 */
void gic_enable_intid(unsigned intid);

void timer_start(unsigned ms);      /* EL1 physical timer, periodic via rearm */
void timer_rearm(void);
void timer_stop(void);
unsigned long timer_ticks(void);    /* IRQs handled so far */

void irq_handler(void);

#endif
