# AArch64_Bare_Metal

A small bare-metal AArch64 program for QEMU's `virt` machine, built up one step at a time: start-up code and a
UART, exception vectors, dropping from EL3 to EL1, the MMU, and the GICv3 with the generic timer. There is no
operating system and no C library. Every line is meant to be readable and explainable, and each step has a
walkthrough written for an engineer who knows RISC-V (RV32) and is learning Armv8-A.

## Status

| Step | What | Walkthrough | State |
|---|---|---|---|
| 1 | Boot: linker script, `start.S`, PL011 UART, printf-lite, print `CurrentEL` | [01-boot](docs/walkthrough/01-boot.md) | branch `p1-boot` |
| 2 | Exception vector tables, register save and restore, ESR decoding (at EL3) | [02-vectors](docs/walkthrough/02-vectors.md) | branch `p1-vectors` |

## Build and run

```
make          # build/kernel.elf (and build/kernel.dis, the disassembly)
make run      # run under QEMU; Ctrl-A then X leaves QEMU
make gdb      # start QEMU halted with a gdb server on :1234 (use gdb-multiarch)
```

Needs `aarch64-linux-gnu-gcc` and `qemu-system-aarch64` (tested with QEMU 8.2.2 and GCC 13).

The machine is

```
qemu-system-aarch64 -M virt,gic-version=3,secure=on,virtualization=on -cpu cortex-a53 -smp 4 -nographic -kernel build/kernel.elf
```

`secure=on` gives the CPU the EL3 and Secure state; `virtualization=on` gives it EL2. Without both, a
`-kernel` image starts at EL1 (or EL2). `-cpu cortex-a53` is QEMU's `-cpu` option value; `-cpu max` also works.

## Memory map

What the program uses of QEMU `virt` (read from QEMU's `info mtree`, not from a document):

| Address | What |
|---|---|
| `0x00000000` | flash (two 64 MB banks), empty, and not used: the ELF is not loaded here |
| `0x08000000` | GICv3 distributor |
| `0x080A0000` | GICv3 redistributors (one 128 KB frame per core) |
| `0x09000000` | PL011 UART0 |
| `0x0E000000` | secure RAM (16 MB), present because of `secure=on`, not used |
| `0x40000000` | start of RAM (128 MB by default) |

QEMU loads the ELF at the addresses in its program headers, so [`linker.ld`](linker.ld) links the image at
`0x40000000`, the start of RAM, with the entry point `_start` as the first byte. With `secure=on` all four
cores start executing at that entry point in EL3h with the interrupts masked, with no boot stub in between
(checked with `info registers` before the first instruction, and by having the secondary cores write to the
UART). Because nothing powers the secondary cores on, they all start at once, and `start.S` parks cores 1 to 3.

Inside RAM the image is: `.text`, `.rodata`, `.data`, `.bss`, then a 16 KB stack whose top is `__stack_top`.

## Layout

| Path | Contents |
|---|---|
| [`Makefile`](Makefile) | build, run, gdb |
| [`linker.ld`](linker.ld) | the memory map and sections |
| [`src/start.S`](src/start.S) | entry: park cores 1-3, stack, zero BSS, call `main` |
| [`src/uart.c`](src/uart.c) | PL011 polled transmit |
| [`src/printf.c`](src/printf.c) | `printf` with `%s %c %d %x %%` |
| [`src/vectors.S`](src/vectors.S) | the three vector tables (EL1, EL2, EL3), save and restore of the frame |
| [`src/exception.c`](src/exception.c) | the C handler: decode ESR and print |
| [`include/trap.h`](include/trap.h), [`include/sysreg.h`](include/sysreg.h) | the trap frame; system register access macros |
| [`src/main.c`](src/main.c) | the tests |
| [`docs/walkthrough/`](docs/walkthrough) | one walkthrough per step |
