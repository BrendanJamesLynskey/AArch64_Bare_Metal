# Repo description

Bare-metal AArch64 on QEMU's `virt` machine, built step by step with no OS and no C library: boot at EL3 with a PL011 UART and printf, exception vectors with ESR decoding, the drop from EL3 to EL1, stage-1 page tables with the MMU and caches on, and the GICv3 with the generic timer. Each step has a walkthrough with RV32 comparisons, check-yourself questions and a break-it exercise, and a headless QEMU smoke test runs in CI.
