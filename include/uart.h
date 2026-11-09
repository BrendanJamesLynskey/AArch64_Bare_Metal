#ifndef UART_H
#define UART_H

/* PL011 UART0 on QEMU `virt`. */
#define UART0_BASE 0x09000000UL

/* The virtual address the driver uses; defaults to UART0_BASE (identity). */
void uart_set_base(unsigned long va);
void uart_putc(char c);
void uart_puts(const char *s);

#endif
