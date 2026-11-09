# Step 4: the MMU

Goal: identity-map the machine with 4 KB-granule stage-1 tables, turn on the MMU, data cache and instruction
cache, and prove translation is happening by reaching the UART at a second virtual address.

Files: [`src/mmu.c`](../../src/mmu.c), [`include/mmu.h`](../../include/mmu.h), the end of
[`src/main.c`](../../src/main.c).

## Translation with a 4 KB granule

A table is 4 KB: 512 entries of 8 bytes. Each level takes 9 bits of the virtual address, and the last 12 bits are
the offset in a page:

| VA bits | Selects | One entry covers |
|---|---|---|
| [38:30] | level-1 entry | 1 GB |
| [29:21] | level-2 entry | 2 MB |
| [20:12] | level-3 entry | 4 KB |
| [11:0] | byte in the page | |

An entry is invalid (bit 0 = 0), a table pointer (bits [1:0] = `0b11`, at levels 0 to 2), a block (`0b01`, at
levels 1 and 2: it maps 1 GB or 2 MB directly), or at level 3 a page (`0b11`). `TCR_EL1.T0SZ = 25` gives a 39-bit
address space, which makes level 1 the first level, so `TTBR0_EL1` points at a level-1 table.

The tables in `mmu.c`:

| VA range | Maps to | How | Type |
|---|---|---|---|
| `0x00000000` to `0x3fffffff` | same PA | level-1 table pointer, then 512 level-2 blocks of 2 MB | Device-nGnRE (flash, GIC, UART) |
| `0x40000000` to `0x7fffffff` | same PA | one level-1 block of 1 GB | Normal write-back (RAM) |
| `0x80000000` to `0x80000fff` | PA `0x09000000` | level 1, level 2, then one level-3 page | Device-nGnRE (the UART again) |
| everything else | | invalid | translation fault |

The second mapping of the same UART at a different VA is the proof: it can only work if the walker is translating.

## Memory attributes

Each descriptor carries a 3-bit index (AttrIndx) into `MAIR_EL1`, a register of eight 8-bit attribute encodings.

| Index | MAIR byte | Meaning | Used for |
|---|---|---|---|
| 0 | `0x00` | Device-nGnRnE | (unused, reserved as the strictest type) |
| 1 | `0x04` | Device-nGnRE | UART, GIC |
| 2 | `0xff` | Normal, inner and outer write-back, read- and write-allocate | RAM |

Device letters: **nG** = non-Gathering (accesses are not merged), **nR** = non-Reordering (they happen in program
order), **E** = Early write acknowledgement is allowed (a write may be acknowledged by a buffer on the way, not
only by the peripheral). nGnRnE forbids the early acknowledgement; nGnRE allows it and is the usual choice for
peripherals. Normal memory may be cached, gathered and reordered.

Other descriptor fields set here: `AF` (access flag, bit 10) must be 1 or the first access takes an access-flag
fault; `SH` (bits [9:8]) shareability: inner shareable for RAM, outer for Device; `PXN`/`UXN` (bits 53, 54) forbid
instruction fetch from Device mappings, so speculation cannot fetch from a peripheral.

## TCR_EL1

| Field | Value | Meaning |
|---|---|---|
| T0SZ | 25 | 39-bit VA for TTBR0 |
| IRGN0, ORGN0 | 1, 1 | table walks use write-back write-allocate Normal memory |
| SH0 | 3 | table walks are inner shareable |
| TG0 | 0 | 4 KB granule |
| EPD1 | 1 | no TTBR1 walks: the upper VA half (`0xffff...`) is not used |
| IPS | 2 | 40-bit physical address size |

## The sequence, and the barriers

1. Build the tables with ordinary stores (MMU still off, so they go straight to memory).
2. Write `MAIR_EL1`, `TCR_EL1`, `TTBR0_EL1`.
3. `dsb ish`: wait until the table stores are complete, so the hardware table walker will see them.
4. `isb`: the register writes must take effect before anything depends on them.
5. `tlbi vmalle1` and `ic iallu`, then `dsb nsh`, `isb`: throw away stale translations and instructions. TLB and
   cache maintenance instructions are not complete until a DSB.
6. Set `SCTLR_EL1` bits M (0), C (2) and I (12), then `isb`.

Since RAM is identity mapped, the instruction after the `msr sctlr_el1` is fetched from the same address, now
translated. If RAM were not mapped (or the mapping were wrong), the CPU would fault at that point with no way to
report it, because the vectors are also in RAM.

`at s1e1r, va` asks the hardware to translate a VA without accessing it; the result is in `PAR_EL1`. The
program prints it: for `0x80000000` it shows PA `0x09000000` with attribute `0x04` (Device-nGnRE).

## What was different from the architecture's text

Nothing found. QEMU does not model caches (C and I change nothing observable), and does not need the barriers
for the result to be correct; they are there because real hardware does.

## RV32 comparison

| RV32 | AArch64 |
|---|---|
| Sv32: two levels, 4 KB and 4 MB pages | 4 KB granule: up to four levels; blocks of 1 GB and 2 MB; here 3 levels (39-bit VA) |
| `satp` (mode + ASID + PPN) | `TTBR0_EL1` (+ `TCR_EL1`) for the lower VA half, `TTBR1_EL1` for the upper |
| PTE flags R, W, X, U, A, D | AP (access permission) and XN bits; AF; no separate D bit unless hardware dirty state is enabled |
| PMP: physical regions, no translation | no equivalent here: stage-1 tables supply permissions; memory type comes from MAIR |
| memory type is fixed by the platform (PMAs) | memory type is chosen per mapping (Device or Normal, cacheability) |
| `sfence.vma` | `tlbi` + `dsb` + `isb` |
| caches are invisible to software | enabling and maintaining caches is software's job (`SCTLR.C`, `.I`, `dc`/`ic`) |

## Check yourself

1. Which VA bits index the level-2 table, and how many bytes does one entry cover?
2. Why is the UART mapped Device and RAM Normal?
3. Why is `dsb ish` needed before turning the MMU on?
4. What happens if you forget `AF` on the RAM block?
5. Why is `EPD1` set?

<details>
<summary>Answers</summary>

1. Bits [29:21]; each entry covers 2 MB (2^21 bytes).
2. Device accesses must not be cached, merged, repeated or speculated; they have side effects. Normal memory can
   be, which is what makes it fast.
3. The table entries were written by stores. Without the barrier the walker could read an old value, because
   the store may not yet be visible to it.
4. The first access to the block takes an access-flag fault. Since the code is in that block, the CPU faults at
   the first fetch after the MMU is enabled, and so does the vector fetch: the program just goes silent.
5. A VA with the top bits set would use `TTBR1_EL1`, which has no tables and a reset value of 0. `EPD1` makes such
   an access a translation fault instead of a walk of garbage.
</details>

## Break it

In `mmu.c` remove `| PTE_AF` from `NORMAL_FLAGS`. Predict: as soon as `SCTLR_EL1.M` is set, the next
instruction fetch takes an access-flag fault, and the vectors are in the same region, so nothing is printed after
`SCTLR_EL1 before`. Run it (it was verified: the output stops there), then put `PTE_AF` back.
