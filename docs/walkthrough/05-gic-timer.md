# Step 5: the GICv3 and the generic timer

Goal: a periodic interrupt. The EL1 physical timer fires every 100 ms; the IRQ handler acknowledges the interrupt,
prints a tick count, rearms the timer and ends the interrupt. After five ticks the program stops the timer.

Files: [`src/gic.c`](../../src/gic.c), [`src/timer.c`](../../src/timer.c), [`include/gic.h`](../../include/gic.h),
the IRQ branch in [`src/exception.c`](../../src/exception.c), the end of [`src/main.c`](../../src/main.c).

## The pieces

| Piece | Where | Role |
|---|---|---|
| Distributor (GICD) | memory-mapped, `0x08000000`, one per system | global enable; routing of shared interrupts (not used for a PPI) |
| Redistributor (GICR) | memory-mapped, `0x080A0000`, two 64 KB frames per core | wake-up state; the core's private interrupts (SGI 0 to 15, PPI 16 to 31): group, priority, enable |
| CPU interface | system registers `ICC_*_EL1`, per core | the priority mask; acknowledge (`IAR`) and end of interrupt (`EOIR`); group enable |
| Generic timer | system registers `CNTP_*_EL0`, per core | counter, comparator, and a level interrupt line |

The timer's output goes to a PPI: INTID 16 + 14 = **30** for the Non-secure physical timer. (29 is the Secure
physical timer, 27 the virtual timer, 26 the hypervisor timer.)

## What is set up, in order

At **EL3** (`gic_el3_setup`), because these are secure-side or EL2 settings:

| Register | Value | Why |
|---|---|---|
| `GICD_CTLR` | `DS`=1, then `ARE`=1 and `EnableGrp1`=1 | `DS` (disable security) gives the GIC a single security state, so EL1 can program everything and Group 1 interrupts do not need Secure/Non-secure splitting. `ARE` (affinity routing) is required to use the system register interface. `EnableGrp1` lets Group 1 interrupts through. Wait for `RWP` (register write pending) to clear after each write. |
| `ICC_SRE_EL3`, then `ICC_SRE_EL2` | `0xf` | `SRE`=1 selects the system register interface; `Enable`=1 lets the level below set its own `SRE`. A level can only set `SRE` if the level above enabled it. |
| `CNTHCTL_EL2` | `3` | `EL1PCTEN` and `EL1PCEN`: EL1 may read the physical counter and use the physical timer. Without them these accesses trap to EL2. |

At **EL1** (`gic_cpu_init`, `gic_enable_intid`):

| Step | Why |
|---|---|
| `GICR_WAKER.ProcessorSleep` = 0, wait for `ChildrenAsleep` = 0 | The redistributor starts asleep and ignores the core. |
| `ICC_SRE_EL1.SRE` = 1 | Use `ICC_*` instead of the memory-mapped CPU interface. |
| `ICC_PMR_EL1` = `0xff` | Priority mask: an interrupt is signalled only if its priority value is lower than the mask. `0xff` lets all through. |
| `ICC_IGRPEN1_EL1` = 1 | Enable Group 1 interrupts at this CPU interface. |
| `GICR_IGROUPR0` bit 30 = 1 | INTID 30 is Group 1. |
| `GICR_IPRIORITYR[30]` = `0x80` | Priority (lower number is more urgent). |
| `GICR_ISENABLER0` bit 30 = 1 | Enable it (write-1-to-set). |

The timer: `CNTP_TVAL_EL0` = frequency / 10 counts; `CNTP_CTL_EL0` = 1 (`ENABLE`, with `IMASK` = 0). The frequency
is read from `CNTFRQ_EL0` (62.5 MHz on QEMU). Finally `msr daifclr, #2` clears the I mask in PSTATE, since exceptions
started masked.

## An interrupt, end to end

1. The counter reaches the compare value; the timer condition (`ISTATUS`) becomes true and the line to PPI 30 goes high.
2. The redistributor and CPU interface see a pending, enabled, Group 1 interrupt with priority below the mask.
3. The core takes an IRQ at EL1: vector entry 5 (current EL, `SP_ELx`, IRQ). `vectors.S` saves the frame, calls
   `exception_handler`, which calls `irq_handler`.
4. `irq_handler` reads `ICC_IAR1_EL1`: that returns the INTID (30) and moves the interrupt from Pending to Active.
   (1023 would mean spurious.)
5. It counts and prints, then writes `CNTP_TVAL_EL0` to rearm. A new compare value in the future drops the
   timer line, so the interrupt does not immediately re-fire.
6. It writes the INTID to `ICC_EOIR1_EL1`. With `EOImode` = 0 that is both the priority drop and the deactivation.
7. `eret` returns to the `wfi` loop in `main`.

## RV32 comparison

| RV32 | AArch64 |
|---|---|
| CLINT: `mtime` and `mtimecmp` memory-mapped, one comparator per hart | Generic timer: `CNTPCT` counter and per-core comparators in system registers; frequency in `CNTFRQ_EL0` |
| PLIC: claim/complete registers read and written by memory access | GICv3: acknowledge/EOI are system registers `ICC_IAR1_EL1` and `ICC_EOIR1_EL1` |
| timer interrupt is a core-local pin (`mip.MTIP`) | timer interrupt is a PPI (INTID 30) routed through the redistributor |
| `mie`, `mstatus.MIE` | `GICR_ISENABLER0`, `ICC_IGRPEN1_EL1`, PSTATE.I (`daifclr`) |
| one global interrupt entry; `mcause` has the cause | IRQ vector entry; the cause is the INTID read from `ICC_IAR1_EL1` |

## Check yourself

1. Why must the handler write `CNTP_TVAL_EL0` (or disable the timer) before the end of interrupt?
2. What is `ICC_IAR1_EL1` and what does reading it do?
3. Why is INTID 30 handled by the redistributor and not the distributor?
4. What does `ICC_PMR_EL1` = `0xff` mean, and what would `0x00` do?
5. Why are `ICC_SRE_EL3` and `ICC_SRE_EL2` written before EL1 runs?

<details>
<summary>Answers</summary>

1. While the timer condition is still true, its line stays high. After EOI the GIC would find INTID 30 pending
   again at once. Writing a new future compare value (or disabling it) removes the condition.
2. The interrupt acknowledge register for Group 1. Reading returns the INTID of the highest-priority pending
   interrupt and changes its state to Active; this is the CPU's "take it" step.
3. It is a private interrupt (a PPI), one copy per core. Private interrupts are configured in each core's redistributor.
   The distributor handles shared peripheral interrupts (SPIs, INTID 32 and above).
4. `0xff` accepts every priority below 255; `0x00` would block every interrupt, because no priority is lower than 0.
5. The SRE enable chain: EL1 can only turn on the system register interface if the levels above allow it, and
   only EL3 and EL2 can set their own `Enable`.
</details>

## Break it

In `timer.c`, replace the `icc_eoir1_el1` write with nothing. Predict: the first interrupt is taken, but never
ended, so it stays Active and the GIC does not signal another one of the same or lower priority: you see tick 1 and no
more. Run it (verified: one tick, then silence). Put the line back.
