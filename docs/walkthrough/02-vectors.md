# Step 2: the exception vector table

Goal: take exceptions, save every register, decode why it happened, and come back. In this step it all runs
at EL3 (the level the program starts in); step 3 moves it to EL1.

Files: [`src/vectors.S`](../../src/vectors.S), [`src/exception.c`](../../src/exception.c),
[`include/trap.h`](../../include/trap.h), [`src/main.c`](../../src/main.c).

## The table

`VBAR_ELx` holds the base address of that level's vector table. The table is 16 entries of 0x80 bytes (32
instructions each) and must be 2 KB aligned, so `VBAR` bits [10:0] are reserved. The entry used depends on two
things the hardware knows:

- where the exception came from, as a group: the same EL using `SP_EL0`, the same EL using `SP_ELx`, a lower EL
  running AArch64, a lower EL running AArch32 (offsets `0x000`, `0x200`, `0x400`, `0x600`);
- what kind it is, in each group: synchronous (0x000), IRQ (0x080), FIQ (0x100), SError (0x180).

Our code runs on `SP_ELx` (the `h` in "EL3h"), so a fault in it uses entry 4 (offset 0x200). Entry 8 (0x400) is
used for exceptions from a lower level, which matters after step 3.

Each 0x80-byte slot is too small for a full handler, so each entry does the minimum and jumps to shared code.
The three tables (`vectors_el1`, `_el2`, `_el3`) come from one assembler macro, which is why `elr_el\el` and
friends read the right registers for the level without testing `CurrentEL`.

## What the entry does

```
sub  sp, sp, #288        make room for a trap_frame on whichever stack was in use
stp  x0, x1, [sp]        save two registers so we have scratch
mov  x0, #n              the entry number
b    exc_common_elN
```

The common code saves `x2..x30`, then reads four system registers and stores them in the frame:

| Register | Meaning |
|---|---|
| `ELR_ELx` | Exception Link Register: the address to return to. For a fault (undefined instruction, abort) it is the faulting instruction. For `svc` it is the next instruction. |
| `SPSR_ELx` | Saved Program Status Register: PSTATE at the time of the exception (EL and stack selection in bits [3:0], interrupt masks in [9:6], condition flags in [31:28]). `eret` copies it back into PSTATE. |
| `ESR_ELx` | Exception Syndrome Register: what happened (below). |
| `FAR_ELx` | Fault Address Register: the faulting virtual address, valid for aborts and alignment faults. |

It then calls `exception_handler(frame, el)` in C. On return it writes `ELR` and `SPSR` back from the frame (the
handler may have changed `ELR`), restores all registers, frees the frame and executes `eret`, which sets
PC from `ELR` and PSTATE from `SPSR`.

The frame is 288 bytes = 36 words, a multiple of 16, so `sp` stays 16-byte aligned. The C struct and the byte
offsets in the assembly must agree; a `_Static_assert` on the size guards one half of that.

## Decoding ESR

`ESR_ELx` has three fields:

| Bits | Name | Meaning |
|---|---|---|
| [31:26] | EC | exception class: what kind of event |
| [25] | IL | instruction length: 1 for a 32-bit instruction (all AArch64 instructions are) |
| [24:0] | ISS | instruction-specific syndrome; its layout depends on EC |

Examples from this program: `svc #0x42` gives EC `0x15` and ISS = the immediate `0x42`; an undefined instruction
gives EC `0x00` ("unknown reason") and ISS 0; a misaligned load gives EC `0x25` (data abort, same EL) with ISS
bits [5:0] = `0x21` (alignment fault) and `FAR` = the address. ISS bit 6 (`WnR`) says whether it was a write.

## What the test does

1. `svc #0x42`: the handler prints the syndrome and returns. `ELR` already points after the `svc`. The test also
   loads `x9` and `x28` with known values and checks them afterwards, which proves the save and restore.
2. `.inst 0x00000000`: an encoding that is permanently undefined. `ELR` is the bad instruction itself, so the
   handler adds 4 to skip it. (Skipping is a test convenience. A real handler would not return to a bad
   instruction.)
3. An 8-byte load from `0x40000001`, with `SCTLR_EL3.A` set so alignment is checked.

## Differences from what the architecture led me to expect

With the MMU off, data accesses are treated as Device memory, and unaligned Device accesses must fault even with
`SCTLR.A` clear. QEMU 8.2.2 does not raise that fault, so the step sets `SCTLR_ELx.A` (bit 1), which is enforced
everywhere.

## RV32 comparison

| RV32 | AArch64 |
|---|---|
| `mtvec` (one base, vectored or direct) | `VBAR_ELx`, a 2 KB table; the hardware chooses the entry from source and type |
| `mcause` (interrupt bit + code) | `ESR_ELx.EC` and `.ISS` for synchronous exceptions; the entry number says IRQ, FIQ or SError |
| `mepc` | `ELR_ELx` |
| `mstatus.MPP`/`MPIE` | `SPSR_ELx` holds the whole saved PSTATE |
| `mtval` | `FAR_ELx` (and ISS for some classes) |
| `mret` | `eret` |
| `ecall` | `svc` (to EL1), `hvc` (to EL2), `smc` (to EL3) |
| no banked stack pointers | `SP_EL0..3`, and exceptions use the current level's `SP_ELx` by default |

## Check yourself

1. Why must the vector table be 2 KB aligned, and what spaces the entries?
2. For `svc`, does ELR point at the `svc` or after it? For an undefined instruction?
3. Why does the entry stub save `x0` and `x1` first?
4. Which entry is used for an IRQ taken while running on `SP_ELx` at the same EL?
5. What are the three fields of ESR, and which one tells you how to read the other?

<details>
<summary>Answers</summary>

1. `VBAR_ELx` has no bits below bit 11, so a table can only start on a 2 KB boundary. Each of the 16 entries is 0x80
   bytes, so entry `n` is at `VBAR + 0x80*n`.
2. After it, because `svc` is a request that completes; for an undefined instruction it points at the bad
   instruction. That is why only the second case needs `ELR += 4` to skip it.
3. No register is free at the moment of the exception: the interrupted code owns all of them. Saving two gives
   scratch space to load the entry number and build the rest of the frame.
4. Entry 5 (offset 0x280).
5. EC, IL and ISS. EC decides how to read ISS.
</details>

## Break it

In `exc_common_el\el` change `ldp x1, x2, [sp, #248]` / the `msr elr_el` write so `ELR` is not written back (delete
the two `msr` lines). Predict: the undefined-instruction test no longer skips, so the same instruction faults
again, forever. Run it and see the same message repeat. Then put the lines back.
