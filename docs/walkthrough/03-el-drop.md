# Step 3: EL3 to EL2 to EL1

Goal: leave the highest exception level the way system software does, land in EL1 with a stack and vectors of
its own, and show that the exceptions of step 2 now arrive at `VBAR_EL1` with `ESR_EL1`, `ELR_EL1` and `FAR_EL1`.

Files: [`src/el_drop.S`](../../src/el_drop.S), [`src/main.c`](../../src/main.c).

## The idea

There is no instruction that sets the current exception level. The only way to a lower level is to return from an
exception: `eret` loads PSTATE (which includes the level) from `SPSR_ELx` and the PC from `ELR_ELx`. So the code
prepares those two registers to describe a return to a level that has never been entered, and executes `eret`. It
does this twice: EL3 to EL2, then EL2 to EL1.

## At EL3

| Register | Value | Why |
|---|---|---|
| `SCR_EL3` | `NS`=1, `HCE`=1, `RW`=1 (`0x501`) | `NS`: the lower level is Non-secure. `HCE`: allow the `hvc` instruction (used later for power-off). `RW`: the next level down is AArch64. Everything else 0: no interrupt routing to EL3, `smc` allowed. |
| `SPSR_EL3` | `0x3c9` | Mode `0b1001` = EL2h: EL2 using `SP_EL2` (the `h`; `t` would mean `SP_EL0`). Bits [9:6] = D, A, I, F all 1: debug, SError, IRQ and FIQ masked. |
| `ELR_EL3` | address of `el2_entry` | Where EL2 starts. |

then `eret`.

## At EL2

| Register | Value | Why |
|---|---|---|
| `HCR_EL2` | `RW`=1 (bit 31) | EL1 is AArch64. Nothing else: no stage-2 translation (`VM`=0), and `IMO`/`FMO`/`AMO`=0 so interrupts are taken at EL1 itself. A hypervisor would set these. |
| `VBAR_EL2` | `vectors_el2` | A table in case something faults at EL2. |
| `SCTLR_EL1` | `0x30d00800` | The system control register of EL1: MMU, caches and alignment off, plus the bits the architecture requires to be 1. Writing a known value avoids depending on the reset state. |
| `SP_EL1` | top of the stack | EL1 gets its stack here: it is written from EL2. Writing `SP_EL1` from EL1 also works but the exception stub would then have no stack to use. |
| `VBAR_EL1` | `vectors_el1` | EL1's vector table. |
| `SPSR_EL2` | `0x3c5` | Mode `0b0101` = EL1h; DAIF masked. |
| `ELR_EL2` | the C function `el1_main` | Where EL1 starts. |

then `eret` again, and `el1_main` runs in EL1 and prints `CurrentEL = 0x4 (EL1)`.

## The three exceptions at EL1

The same tests as step 2 are run at EL3 first and then at EL1. At EL1 the output is the same classes with
`_EL1` registers. An `svc` at EL1 is taken at EL1 (the same level), through entry 4, not 8: entry 8 is for an
exception coming from a lower level, such as an `svc` from EL0, which this program does not run.

For the alignment fault the test sets `SCTLR_EL1.A`; `ESR_EL1` shows EC `0x25`, ISS[5:0] = `0x21`, and
`FAR_EL1` = `0x40000001`.

`FAR_EL1` is only meaningful for aborts and alignment faults. The other two lines print whatever it held.

## RV32 comparison

| RV32 | AArch64 |
|---|---|
| `mstatus.MPP` = S, `mepc` = target, `mret` | `SPSR_EL3` = EL2h, `ELR_EL3` = target, `eret` |
| `medeleg`/`mideleg` decide which traps go to S-mode | `HCR_EL2` bits (`IMO`, `FMO`, `AMO`, `TGE`) and `SCR_EL3` bits (`IRQ`, `FIQ`, `EA`) route to a higher level; synchronous exceptions go to the level they came from, or to EL1 by default for `svc`, from EL0 |
| `mtvec`, `stvec` | `VBAR_EL3`, `VBAR_EL2`, `VBAR_EL1` |
| `ecall` from S goes to M | `hvc` from EL1 goes to EL2, `smc` from EL1 or EL2 goes to EL3 |
| `sscratch` swap for a kernel stack | `SP_EL1` is a separate register; no swap |

## Check yourself

1. Why can't you write a register to change from EL3 to EL2?
2. Which `SPSR_EL3` bits chose EL2 rather than EL1, and what do they mean by `h`?
3. What would happen at the `eret` to EL1 if `HCR_EL2.RW` were 0?
4. At EL1, which vector table entry takes an `svc #0x42` executed at EL1?
5. For which of the three tests is `FAR_EL1` meaningful?

<details>
<summary>Answers</summary>

1. The level is part of PSTATE, and PSTATE can only be changed by taking an exception or returning from one.
   This keeps software at a lower level from raising its own privilege.
2. `SPSR_EL3.M[3:0]` = `0b1001`. The `h` means the level uses its own stack pointer (`SP_ELx`); the `t` forms use
   `SP_EL0`.
3. The return would be to AArch32 EL1, but `SPSR_EL2.M[4]` is 0 (the AArch64 encoding), so it does not describe a legal state. The result is an illegal exception return: PSTATE.IL is set and the first instruction takes an exception.
4. Entry 4 (current EL, `SP_ELx`, Sync, at offset 0x200).
5. Only the unaligned access. The `svc` and the undefined instruction do not set `FAR`.
</details>

## Break it

In `el_drop.S` change `SPSR_EL2` from `0x3c5` to `0x3c4` (EL1t: EL1 using `SP_EL0`). Predict: the code runs at EL1
but its stack pointer is now `SP_EL0`, which nothing set (it is 0), so the first function that pushes a frame
faults. Run it: the output starts with an exception from entry 4, then continues with a data abort from entry 0
(a synchronous exception taken on `SP_EL0`) whose own stub needs the same bad stack, so the output is a chain of
exceptions that does not recover. Restore `0x3c5`.
