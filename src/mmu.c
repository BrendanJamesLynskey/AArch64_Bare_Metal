/* Stage 1 translation at EL1: identity-mapped tables, 4 KB granule, 39-bit
 * virtual addresses, then switch the MMU and caches on.
 *
 * With a 4 KB granule each table is 4 KB = 512 entries of 8 bytes, and each
 * level resolves 9 address bits:
 *     VA bits [38:30] index the level-1 table  (each entry covers 1 GB)
 *     VA bits [29:21] index the level-2 table  (each entry covers 2 MB)
 *     VA bits [20:12] index the level-3 table  (each entry covers 4 KB)
 *     VA bits [11:0]  are the offset in the page
 * A 39-bit VA space (TCR.T0SZ = 64 - 39 = 25) makes level 1 the first level.
 *
 * The tables built here:
 *   L1[0]  -> L2 table A   VA 0x00000000-0x3fffffff   identity, 2 MB blocks, Device
 *                          (flash, GICv3, UART, virtio all live here)
 *   L1[1]  = 1 GB block    VA 0x40000000-0x7fffffff   identity, Normal write-back (RAM)
 *   L1[2]  -> L2 table B   VA 0x80000000-0xbfffffff
 *      L2B[0] -> L3 table  VA 0x80000000-0x801fffff
 *         L3[0] = 4 KB page: VA 0x80000000 -> PA 0x09000000 (the UART), Device
 *   everything else: invalid (a translation fault)
 */
#include "mmu.h"
#include "printf.h"
#include "sysreg.h"

/* Page table entry bits. */
#define PTE_VALID    (1UL << 0)
#define PTE_TABLE    (1UL << 1)   /* at L0-L2: next-level table; at L3 this must be 1 for a page */
#define PTE_ATTR(i)  ((unsigned long)(i) << 2)    /* index into MAIR_EL1 */
#define PTE_SH_OUTER (2UL << 8)   /* shareability: outer */
#define PTE_SH_INNER (3UL << 8)   /* shareability: inner */
#define PTE_AF       (1UL << 10)  /* access flag: set, or the first access faults */
#define PTE_PXN      (1UL << 53)  /* privileged execute never */
#define PTE_UXN      (1UL << 54)  /* unprivileged execute never */

/* MAIR_EL1 attribute slots, and the 8-bit encodings put in them. */
#define ATTR_DEVICE_nGnRnE 0
#define ATTR_DEVICE_nGnRE  1
#define ATTR_NORMAL_WB     2
#define MAIR_VALUE ((0x00UL << (8 * ATTR_DEVICE_nGnRnE)) | \
                    (0x04UL << (8 * ATTR_DEVICE_nGnRE))  | \
                    (0xffUL << (8 * ATTR_NORMAL_WB)))

/* Device memory, never executed from. */
#define DEVICE_FLAGS (PTE_VALID | PTE_ATTR(ATTR_DEVICE_nGnRE) | PTE_SH_OUTER | PTE_AF | PTE_PXN | PTE_UXN)
/* Normal memory, write-back, read/write-allocate, inner shareable, executable. */
#define NORMAL_FLAGS (PTE_VALID | PTE_ATTR(ATTR_NORMAL_WB) | PTE_SH_INNER | PTE_AF)

/* Each table must be 4 KB aligned. They are in .bss, which start.S zeroed, so
 * every entry starts out invalid (bit 0 = 0). */
static unsigned long l1[512]   __attribute__((aligned(4096)));
static unsigned long l2_a[512] __attribute__((aligned(4096)));
static unsigned long l2_b[512] __attribute__((aligned(4096)));
static unsigned long l3[512]   __attribute__((aligned(4096)));

void mmu_init(void)
{
    /* Level 1. A table descriptor is the next table's address with bits [1:0] = 0b11.
     * A block descriptor has bits [1:0] = 0b01. */
    l1[0] = (unsigned long)l2_a | PTE_VALID | PTE_TABLE;
    l1[1] = 0x40000000UL | NORMAL_FLAGS;                  /* 1 GB block of RAM */
    l1[2] = (unsigned long)l2_b | PTE_VALID | PTE_TABLE;

    /* Level 2 table A: 512 x 2 MB blocks covering the first 1 GB, as Device. */
    for (unsigned long i = 0; i < 512; i++)
        l2_a[i] = (i << 21) | DEVICE_FLAGS;

    /* Level 2 table B: only entry 0 is used; it points at a level-3 table. */
    l2_b[0] = (unsigned long)l3 | PTE_VALID | PTE_TABLE;

    /* Level 3: a 4 KB page. Bits [1:0] = 0b11 means "page" at this level. */
    l3[0] = 0x09000000UL | DEVICE_FLAGS | PTE_TABLE;

    /* MAIR_EL1: the memory attribute slots the descriptors point at. */
    write_sysreg(mair_el1, MAIR_VALUE);

    /* TCR_EL1: how to translate TTBR0 addresses.
     *   T0SZ [5:0]    = 25      39-bit VA space
     *   IRGN0 [9:8]   = 1       table walks: Normal, inner write-back, write-allocate
     *   ORGN0 [11:10] = 1       outer write-back, write-allocate
     *   SH0 [13:12]   = 3       table walks are inner shareable
     *   TG0 [15:14]   = 0       4 KB granule for TTBR0
     *   EPD1 [23]     = 1       do not walk TTBR1 (upper half): we have no tables for it
     *   IPS [34:32]   = 2       40-bit physical addresses (QEMU's -cpu cortex-a53 has 40) */
    unsigned long tcr = 25UL | (1UL << 8) | (1UL << 10) | (3UL << 12) | (1UL << 23) | (2UL << 32);
    write_sysreg(tcr_el1, tcr);

    /* TTBR0_EL1: the physical address of the level-1 table (we are identity
     * mapped, so that is also its address). */
    write_sysreg(ttbr0_el1, (unsigned long)l1);

    /* Barriers. First, DSB ISH: make sure the table stores above have
     * completed, so the hardware walker will see them. Then ISB: the writes to
     * MAIR/TCR/TTBR0 are context-changing, so they must have taken effect
     * before the next instruction (the TLB invalidate and the MMU enable). */
    __asm__ volatile("dsb ish" ::: "memory");
    isb();

    /* Drop any stale translations and instruction cache contents. The tlbi
     * and ic instructions are only complete after a DSB, and a following ISB
     * makes the pipeline refetch. */
    __asm__ volatile("tlbi vmalle1\n ic iallu\n dsb nsh\n isb" ::: "memory");

    /* SCTLR_EL1: bit 0 M (MMU on), bit 2 C (data cache on), bit 12 I (instruction cache on).
     * This write is the moment translation starts. The next instruction
     * fetch is already translated; our identity mapping of RAM means it
     * is fetched from the same address as before. */
    write_sysreg(sctlr_el1, read_sysreg(sctlr_el1) | (1UL << 0) | (1UL << 2) | (1UL << 12));
    isb();                    /* nothing after this may run with the old setting */
}
