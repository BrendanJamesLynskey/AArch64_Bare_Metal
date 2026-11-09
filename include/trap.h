#ifndef TRAP_H
#define TRAP_H

/* The register state saved by the vector code on every exception.
 * vectors.S builds exactly this layout on the stack; the byte offsets
 * are written out there, and the assert below keeps the two in step. */
struct trap_frame {
    unsigned long x[31];   /* x0..x30, at byte offsets 0..240 */
    unsigned long elr;     /* 248: where to return to (ELR_ELx)   */
    unsigned long spsr;    /* 256: saved PSTATE (SPSR_ELx)        */
    unsigned long esr;     /* 264: syndrome (ESR_ELx)             */
    unsigned long far;     /* 272: faulting address (FAR_ELx)     */
    unsigned long vec;     /* 280: which of the 16 vector entries */
};

_Static_assert(sizeof(struct trap_frame) == 288, "vectors.S assumes 288 bytes");

/* Called from vectors.S with the frame and the EL the exception was taken to. */
void exception_handler(struct trap_frame *f, unsigned long el);

/* Vector tables, one per exception level (see vectors.S). */
extern char vectors_el1[], vectors_el2[], vectors_el3[];

/* Set when a handler decides to return past the faulting instruction. */
const char *esr_class_name(unsigned long ec);

#endif
