/* The C side of exception handling: decode ESR and print it. */
#include "gic.h"
#include "printf.h"
#include "sysreg.h"
#include "trap.h"

void psci_el3_handle(struct trap_frame *f);     /* psci.c */

/* Names for the 16 vector entries, in table order. */
static const char *const vec_name[16] = {
    "current EL, SP_EL0, Sync",  "current EL, SP_EL0, IRQ",
    "current EL, SP_EL0, FIQ",   "current EL, SP_EL0, SError",
    "current EL, SP_ELx, Sync",  "current EL, SP_ELx, IRQ",
    "current EL, SP_ELx, FIQ",   "current EL, SP_ELx, SError",
    "lower EL (AArch64), Sync",  "lower EL (AArch64), IRQ",
    "lower EL (AArch64), FIQ",   "lower EL (AArch64), SError",
    "lower EL (AArch32), Sync",  "lower EL (AArch32), IRQ",
    "lower EL (AArch32), FIQ",   "lower EL (AArch32), SError",
};

/* ESR_ELx.EC, bits [31:26]: the exception class. The common ones. */
const char *esr_class_name(unsigned long ec)
{
    switch (ec) {
    case 0x00: return "unknown reason (undefined instruction)";
    case 0x01: return "trapped WFI/WFE";
    case 0x07: return "trapped FP/SIMD access";
    case 0x0e: return "illegal execution state";
    case 0x15: return "SVC from AArch64";
    case 0x16: return "HVC from AArch64";
    case 0x17: return "SMC from AArch64";
    case 0x18: return "trapped MSR/MRS/system instruction";
    case 0x20: return "instruction abort, lower EL";
    case 0x21: return "instruction abort, same EL";
    case 0x22: return "PC alignment fault";
    case 0x24: return "data abort, lower EL";
    case 0x25: return "data abort, same EL";
    case 0x26: return "SP alignment fault";
    case 0x2f: return "SError";
    case 0x30: return "breakpoint, lower EL";
    case 0x31: return "breakpoint, same EL";
    case 0x3c: return "BRK instruction";
    default:   return "other class";
    }
}

/* For aborts, ISS[5:0] is the fault status code (DFSC/IFSC). */
static const char *fault_status_name(unsigned long fsc)
{
    switch (fsc) {
    case 0x00: case 0x01: case 0x02: case 0x03: return "address size fault";
    case 0x04: case 0x05: case 0x06: case 0x07: return "translation fault";
    case 0x09: case 0x0a: case 0x0b: return "access flag fault";
    case 0x0d: case 0x0e: case 0x0f: return "permission fault";
    case 0x10: return "synchronous external abort";
    case 0x21: return "alignment fault";
    default:   return "other fault";
    }
}

void exception_handler(struct trap_frame *f, unsigned long el)
{
    /* IRQ entries are 1, 5, 9 and 13. Handled quietly: a tick is not an error. */
    if ((f->vec & 3) == 1) {
        irq_handler();
        return;
    }

    unsigned long ec  = (f->esr >> 26) & 0x3f;

    /* An SMC that reaches EL3 is a firmware call (PSCI): serve it quietly. */
    if (el == 3 && ec == 0x17) {
        psci_el3_handle(f);
        return;
    }
       /* exception class */
    unsigned long il  = (f->esr >> 25) & 1;          /* 1 = 32-bit instruction */
    unsigned long iss = f->esr & 0x1ffffff;          /* class-specific detail */

    printf("\n*** exception at EL%d, entry %d: %s\n", (int)el, (int)f->vec, vec_name[f->vec]);
    printf("    ESR_EL%d=0x%lx EC=0x%lx (%s) IL=%d ISS=0x%lx\n", (int)el, f->esr, ec,
           esr_class_name(ec), (int)il, iss);

    if (ec == 0x24 || ec == 0x25 || ec == 0x20 || ec == 0x21) {
        printf("    %s%s\n", fault_status_name(iss & 0x3f),
               (ec == 0x24 || ec == 0x25) ? ((iss & 0x40) ? " on a write" : " on a read") : "");
    }
    if (ec == 0x15 || ec == 0x16 || ec == 0x17 || ec == 0x3c)
        printf("    immediate = 0x%lx\n", iss & 0xffff);   /* svc/hvc/smc/brk #imm */
    /* FAR is only meaningful for aborts and alignment faults; for the others
     * it holds an unknown value. */
    printf("    ELR_EL%d=0x%lx FAR_EL%d=0x%lx SPSR=0x%lx\n", (int)el, f->elr, (int)el, f->far, f->spsr);

    /* Only synchronous exceptions taken here are expected, and they come
     * from deliberate test instructions. Return to the instruction after
     * the one that faulted. (SVC is different in hardware: ELR already
     * points after the svc. Faults point at the faulting instruction.) */
    if ((f->vec & 3) == 0) {
        if (ec != 0x15)
            f->elr += 4;
        return;
    }

    /* Anything else (IRQ, FIQ, SError) is not expected in this step. */
    printf("    unexpected: stopping\n");
    for (;;)
        __asm__ volatile("wfe");
}
