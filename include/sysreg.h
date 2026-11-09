#ifndef SYSREG_H
#define SYSREG_H

/* Read and write a system register by name:  read_sysreg(esr_el1)  */
#define read_sysreg(reg) ({                                   \
        unsigned long _v;                                     \
        __asm__ volatile("mrs %0, " #reg : "=r"(_v));         \
        _v; })

#define write_sysreg(reg, val) do {                           \
        unsigned long _v = (unsigned long)(val);              \
        __asm__ volatile("msr " #reg ", %0" :: "r"(_v));      \
    } while (0)

/* Instruction synchronisation barrier: makes sure everything after it sees
 * the effect of the system register writes before it. */
#define isb() __asm__ volatile("isb" ::: "memory")
#define dsb_sy() __asm__ volatile("dsb sy" ::: "memory")

#endif
