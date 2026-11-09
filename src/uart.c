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

#define UARTDR (*(volatile unsigned int *)(UART0_BASE + 0x00))
#define UARTFR (*(volatile unsigned int *)(UART0_BASE + 0x18))
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
