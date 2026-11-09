# Step 1: boot, the UART, and CurrentEL

Goal: get from the first instruction to a C `main` that prints, and learn which exception level you are in.

Files: [`src/start.S`](../../src/start.S), [`linker.ld`](../../linker.ld), [`src/uart.c`](../../src/uart.c),
[`src/printf.c`](../../src/printf.c), [`src/main.c`](../../src/main.c).

## What happens at reset

QEMU loads the ELF at `0x40000000` and sets every core's program counter to the ELF entry point. With
`secure=on` the cores are in EL3 (the highest exception level, where secure monitor firmware would normally run),
using `SP_EL3` ("EL3h"), with D, A, I and F (debug, SError, IRQ, FIQ) masked. On real silicon the reset address
comes from the `RVBAR` register or from the SoC; QEMU just uses the ELF entry.

## `start.S`, line by line

| Instruction | Why |
|---|---|
| `mrs x0, mpidr_el1` | Read the core's identity. Affinity fields Aff0, Aff1, Aff2 are bits [7:0], [15:8], [23:16]; on `virt` Aff0 is the core number. |
| `and x0, x0, #0xffffff` | Keep Aff2:Aff1:Aff0 and drop the flag bits (bit 31 is always 1, bit 24 is MT). |
| `cbnz x0, park` | Any core that is not core 0 jumps to the park loop. |
| `ldr x0, =__stack_top` / `mov sp, x0` | The linker script defines the symbol; `ldr =` loads a 64-bit constant from a literal pool. `sp` here is `SP_EL3`: each exception level has its own stack pointer register. |
| `stp xzr, xzr, [x0], #16` | Store a pair of zeros and then add 16 to `x0` (post-index). `xzr` reads as zero. Clears the BSS 16 bytes per iteration. |
| `bl main` | Branch and link: the return address goes in `x30` (the link register), `x0` to `x7` carry arguments. |
| `wfe` loop | Wait For Event: a low-power wait. It is a hint, so it is put in a loop. |

The `sp` rule: whenever `sp` is the base of a load or store, it must be 16-byte aligned, or the core takes an
alignment fault (if `SCTLR_ELx.SA` is set, which it is out of reset on this CPU in QEMU's model). That is why the
stack is 16-byte aligned and the BSS loop uses 16-byte stores.

## The linker script

One memory region at `0x40000000`. `.text.boot` is listed first with `KEEP`, so `_start` is at the first byte
and cannot be discarded. `.bss` is `NOLOAD` (it takes no space in the file) and its start and end are
exported for the zeroing loop. The stack is placed after it.

## The PL011 UART

A PL011 has dozens of registers; transmit needs two. `UARTDR` at offset 0 takes the byte; bit 5 of `UARTFR`
(offset 0x18) is TXFF, "transmit FIFO full". Poll until it is clear, then write. The pointers are `volatile` so
the compiler performs every access in order and does not cache the flag.
QEMU's model transmits without any baud setup or enable bit. Hardware would need `UARTIBRD`, `UARTFBRD`, `UARTLCR_H` and
`UARTCR` first.

## printf-lite

`printf` walks the format string. `%d` and `%x` take 32 bits unless you write `%ld` / `%lx`. Numbers are built
in a small buffer least-significant digit first and then printed backwards. There is no `malloc` and no
floating point (the compiler flag `-mgeneral-regs-only` forbids it, because nothing has enabled the FP/SIMD
unit; if you used it you would trap).

## CurrentEL

`mrs x, CurrentEL` returns the level in bits [3:2], so you get `0x4 * EL`. This program prints
`CurrentEL = 0xc (EL3)`.

## RV32 comparison

| RV32 | AArch64 |
|---|---|
| `csrr a0, mhartid`, park harts that are not 0 | `mrs x0, mpidr_el1`, mask, park cores that are not 0 |
| M, S, U privilege modes | EL3, EL2, EL1, EL0: four levels, and EL3/EL2 are optional |
| reset in M-mode, one stack pointer `sp` | reset in the highest implemented EL, and each level has its own `SP_ELx` |
| `auipc`/`addi` or `la` for symbol addresses | `ldr x, =sym` (literal pool) or `adrp` + `add` |
| no way to read the current mode from software | `CurrentEL` tells you (EL0 has no access to it) |

## Check yourself

1. Why does `start.S` mask MPIDR_EL1 before comparing with zero?
2. What would happen to a store through `sp` if `sp` were `0x40010008`?
3. Why is `.bss` zeroed by hand here, when a hosted C program gets it for free?
4. Why are the UART pointers `volatile`?
5. What does `CurrentEL` read as in EL2?

<details>
<summary>Answers</summary>

1. The register has other bits set (bit 31 reads as 1, and bit 24 flags a multithreaded core). Comparing the
   whole register would send core 0 to the park loop.
2. It is not 16-byte aligned, so the access takes an SP alignment fault (a synchronous exception). Here that
   would be a fatal loop with no vectors installed yet.
3. Nothing loads the program the way an OS does: QEMU copies only the file's contents, and the BSS has no file
   contents. C requires static variables to start as zero, so start-up code must do it.
4. Without it the compiler may remove repeated reads of the flag, merge writes or reorder them. Device registers
   have side effects that the compiler cannot see.
5. `0x8`.
</details>

## Break it

Delete the `cbnz x0, park` line and run `make run`. Predict first: all four cores now set the same stack
pointer and run `main`, so the output is mixed. Then run it. Try `-smp 1` to see that the bug hides on one core.
