/* PL011 UART driver, transmit only, polled.
 *
 * The PL011 is a memory-mapped peripheral. We use two registers:
 *   UARTDR (offset 0x00)  write a byte here to send it
 *   UARTFR (offset 0x18)  flag register; bit 5 (TXFF) is set while the
 *                         transmit FIFO is full
 * QEMU's model does not need the baud rate or the UART enabled to transmit,
 * so we do no initialisation. On real hardware we would set the baud divisor
 * and UARTCR first.
 */
#include "uart.h"

/* The base can change: after the MMU is on, the UART is also reachable at
 * a second virtual address (see mmu.c). */
static unsigned long uart_base = UART0_BASE;

void uart_set_base(unsigned long va)
{
    uart_base = va;
}

#define UARTDR (*(volatile unsigned int *)(uart_base + 0x00))
#define UARTFR (*(volatile unsigned int *)(uart_base + 0x18))
#define UARTFR_TXFF (1u << 5)

void uart_putc(char c)
{
    /* Wait until there is room in the TX FIFO, then write the byte.
     * `volatile` stops the compiler caching or dropping these accesses. */
    while (UARTFR & UARTFR_TXFF)
        ;
    UARTDR = (unsigned int)c;
}

void uart_puts(const char *s)
{
    while (*s) {
        /* Terminals want CR LF; the C code just writes \n. */
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}
