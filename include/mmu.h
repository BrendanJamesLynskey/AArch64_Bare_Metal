#ifndef MMU_H
#define MMU_H

/* A second virtual address that maps the UART (physical 0x09000000), used to
 * show that the MMU is really translating. Nothing is at physical 0x80000000. */
#define UART_ALIAS_VA 0x80000000UL

void mmu_init(void);

#endif
