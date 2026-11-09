#ifndef PRINTF_H
#define PRINTF_H

/* printf-lite. Supports %s %c %d %x and %%, and the 'l' length modifier
 * (%ld %lx) for 64-bit values. Without 'l', %d and %x take a 32-bit int. */
void printf(const char *fmt, ...);

#endif
