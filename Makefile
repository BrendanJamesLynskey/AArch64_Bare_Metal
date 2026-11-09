# Bare-metal AArch64 on QEMU's `virt` machine.
#   make        build build/kernel.elf
#   make run    run it under QEMU (Ctrl-A then X leaves QEMU)
#   make gdb    start QEMU halted, waiting for gdb on :1234
#   make clean

CROSS   ?= aarch64-linux-gnu-
CC      := $(CROSS)gcc
OBJDUMP := $(CROSS)objdump

# -ffreestanding -nostdlib: no C library, no start-up files; we provide both.
# -mgeneral-regs-only: the compiler must not touch the FP/SIMD registers,
#   because nothing here enables them (CPACR_EL1/CPTR_EL3 are left alone).
# -mstrict-align: no unaligned accesses. Once the MMU is on and memory is
#   Device type, an unaligned access faults, so the compiler must never emit one.
CFLAGS  := -Wall -Wextra -O1 -g -ffreestanding -nostdlib -fno-pic \
           -mgeneral-regs-only -mstrict-align -mcpu=cortex-a53 -Iinclude
ASFLAGS := -g -Iinclude -mcpu=cortex-a53
LDFLAGS := -nostdlib -static -T linker.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments

SRCS_C  := $(wildcard src/*.c)
SRCS_S  := $(wildcard src/*.S)
OBJS    := $(patsubst src/%.c,build/%.o,$(SRCS_C)) $(patsubst src/%.S,build/%.o,$(SRCS_S))

# Machine: the EL3 start needs secure=on; EL2 needs virtualization=on.
QEMU    ?= qemu-system-aarch64
QFLAGS  := -M virt,gic-version=3,secure=on,virtualization=on -cpu cortex-a53 -smp 4 \
           -nographic -kernel build/kernel.elf

all: build/kernel.elf

build/kernel.elf: $(OBJS) linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)
	$(OBJDUMP) -d $@ > build/kernel.dis

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/%.o: src/%.S | build
	$(CC) $(ASFLAGS) -c -o $@ $<

build:
	mkdir -p build

run: all
	$(QEMU) $(QFLAGS)

gdb: all
	$(QEMU) $(QFLAGS) -S -s

clean:
	rm -rf build

.PHONY: all run gdb clean
