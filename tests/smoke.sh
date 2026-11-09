#!/usr/bin/env bash
# Smoke test: build, run the program headless under QEMU, and check the UART output for the
# lines each step must produce. The program ends itself with PSCI SYSTEM_OFF, so QEMU must exit
# with status 0 well inside the timeout; a hang shows up as a timeout (status 124).
#
#   tests/smoke.sh              run with -cpu cortex-a53 (the default)
#   CPU=max tests/smoke.sh      run with another -cpu value
#   QEMU=... TIMEOUT=... override the emulator binary / the time limit in seconds
set -u
cd "$(dirname "$0")/.."

QEMU=${QEMU:-qemu-system-aarch64}
CPU=${CPU:-cortex-a53}
TIMEOUT=${TIMEOUT:-30}
OUT=build/smoke.out

make -s all || { echo "FAIL build"; exit 1; }

# -smp 4 so that the secondary cores are really parked by start.S.
timeout "$TIMEOUT" "$QEMU" -M virt,gic-version=3,secure=on,virtualization=on -cpu "$CPU" -smp 4 \
    -nographic -kernel build/kernel.elf > "$OUT" 2>&1 < /dev/null
rc=$?
tr -d '\r' < "$OUT" > "$OUT.txt"

fails=0
pass() { echo "PASS  $1"; }
fail() { echo "FAIL  $1"; fails=$((fails + 1)); }

# check "<step>" "<text that must appear>"
check() {
    if grep -qF -- "$2" "$OUT.txt"; then pass "$1: $2"; else fail "$1: missing '$2'"; fi
}
# check_not "<step>" "<text that must not appear>"
check_not() {
    if grep -qF -- "$2" "$OUT.txt"; then fail "$1: found '$2'"; else pass "$1: no '$2'"; fi
}

# The run ended by itself.
if [ "$rc" -eq 0 ]; then pass "exit: QEMU exited with status 0 (PSCI SYSTEM_OFF)"
else fail "exit: QEMU status $rc (124 = timed out)"; fi

# Step 1: boot, UART, CurrentEL
check 1-boot "AArch64 bare metal on QEMU virt"
check 1-boot "CurrentEL = 0xc (EL3)"
# The secondary cores parked: the banner appears exactly once.
n=$(grep -cF "AArch64 bare metal on QEMU virt" "$OUT.txt")
if [ "$n" -eq 1 ]; then pass "1-boot: banner printed once (cores 1-3 parked)"; else fail "1-boot: banner printed $n times"; fi

# Step 2: vectors and ESR decoding, at EL3
check 2-vectors "ESR_EL3=0x56000042 EC=0x15 (SVC from AArch64) IL=1 ISS=0x42"
check 2-vectors "immediate = 0x42"
check 2-vectors "back after svc; registers preserved"
check 2-vectors "ESR_EL3=0x2000000 EC=0x0 (unknown reason (undefined instruction))"
check 2-vectors "ESR_EL3=0x96000021 EC=0x25 (data abort, same EL)"
check 2-vectors "alignment fault on a read"

# Step 3: EL drop, the same exceptions at EL1
check 3-el-drop "CurrentEL = 0x4 (EL1)"
check 3-el-drop "ESR_EL1=0x56000042 EC=0x15 (SVC from AArch64) IL=1 ISS=0x42"
check 3-el-drop "ESR_EL1=0x2000000 EC=0x0 (unknown reason (undefined instruction))"
check 3-el-drop "ESR_EL1=0x96000021 EC=0x25 (data abort, same EL)"
check 3-el-drop "FAR_EL1=0x40000001"
check 3-el-drop "back after unaligned access"

# Step 4: MMU
check 4-mmu "(M=1 C=1 I=1)"
check 4-mmu "-> PA 0x9000000"
check 4-mmu "hello through the second VA 0x80000000"
check 4-mmu "translation fault on a read"
check 4-mmu "FAR_EL1=0x100000000"
check 4-mmu "back after translation fault"

# Step 5: GICv3 and timer
for i in 1 2 3 4 5; do check 5-gic-timer "tick $i ("; done
check 5-gic-timer "done: 5 ticks"
check_not 5-gic-timer "unexpected INTID"

# Step 6: PSCI
check 6-ci "PSCI VERSION = 0x10000"
check 6-ci "PSCI SYSTEM_OFF"
check_not 6-ci "PSCI SYSTEM_OFF returned"
check_not general "CORRUPTED"

if [ "$fails" -eq 0 ]; then
    echo "smoke: all checks passed (-cpu $CPU)"
else
    echo "smoke: $fails check(s) FAILED (-cpu $CPU); output in $OUT.txt"
    exit 1
fi
