/* printf-lite: %s %c %d %x %% with an optional 'l' (64-bit) modifier.
 * No width, no padding, no floating point. Small enough to read in one go. */
#include <stdarg.h>
#include "printf.h"
#include "uart.h"

static void put_char(char c)
{
    if (c == '\n')
        uart_putc('\r');
    uart_putc(c);
}

/* Print `v` in the given base (10 or 16). Digits come out least significant
 * first, so collect them in a buffer and emit them backwards. */
static void put_unsigned(unsigned long v, unsigned base)
{
    char buf[24];               /* 2^64 has 20 decimal digits */
    int n = 0;

    if (v == 0)
        buf[n++] = '0';
    while (v) {
        unsigned d = v % base;
        buf[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    }
    while (n)
        put_char(buf[--n]);
}

void printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put_char(*fmt);
            continue;
        }
        fmt++;

        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            while (*s)
                put_char(*s++);
            break;
        }
        case 'c':
            put_char((char)va_arg(ap, int));
            break;
        case 'd': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (v < 0) {
                put_char('-');
                put_unsigned(0UL - (unsigned long)v, 10);
            } else {
                put_unsigned((unsigned long)v, 10);
            }
            break;
        }
        case 'x': {
            unsigned long v = is_long ? va_arg(ap, unsigned long)
                                      : (unsigned long)va_arg(ap, unsigned int);
            put_unsigned(v, 16);
            break;
        }
        case '%':
            put_char('%');
            break;
        case '\0':
            va_end(ap);
            return;
        default:                /* unknown conversion: show it, don't eat args */
            put_char('%');
            put_char(*fmt);
            break;
        }
    }
    va_end(ap);
}
