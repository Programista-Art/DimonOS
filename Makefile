CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2

BM_CC = gcc -m32
BM_CFLAGS = -std=c99 -Wall -Wextra -O2 -ffreestanding -nostdlib -DBAREMETAL -fno-pie -fno-stack-protector
BM_LD = ld -m elf_i386 -z noexecstack

ALL = dimon-as dimon-emu dimon-mkiso

all: $(ALL)

dimon-as: assembler.c dimon16.h
	$(CC) $(CFLAGS) -o dimon-as assembler.c

dimon-emu: emulator.c vm.c dimon16.h font8x16.h
	$(CC) $(CFLAGS) -o dimon-emu emulator.c vm.c -lX11

dimon-mkiso: mkiso.c dimon16.h
	$(CC) $(CFLAGS) -o dimon-mkiso mkiso.c

# Build Operating Systems (GUI v2.0 and CLI v1.0) and examples
os.bin: $(ALL) os.asm gui/*.asm apps/*.asm
	./dimon-as os.asm -o os.bin

progs: $(ALL) os.bin
	./dimon-as examples/cli_os.asm -o examples/cli_os.bin
	./dimon-as examples/hello.asm -o examples/hello.bin
	./dimon-as examples/math.asm -o examples/math.bin
	./dimon-as examples/fib.asm -o examples/fib.bin
	./dimon-as examples/stack.asm -o examples/stack.bin
	./dimon-as examples/input.asm -o examples/input.bin

test: progs
	@echo "--- hello ---"
	./dimon-emu examples/hello.bin
	@echo "--- math (expected: 35 42 42 42 48 255 85 32 8) ---"
	./dimon-emu examples/math.bin
	@echo "--- fib ---"
	./dimon-emu examples/fib.bin
	@echo "--- stack (expected: 30 20 10 49) ---"
	./dimon-emu examples/stack.bin
	@echo "--- cli_os: HELP + HALT ---"
	printf 'HELP\nHALT\n' | ./dimon-emu examples/cli_os.bin

gui-test: progs
	@echo "--- DimonOS v2.1 GUI TUI auto-test (Calculator: 25*4=100 -> Exit) ---"
	printf '125*4=\033q' | ./dimon-emu --tui os.bin

# ISO / Virtual disk images (DIMON-ISO, 512B sectors)
dimon.iso: $(ALL) progs examples/iso_boot.asm examples/disk_demo.asm examples/iso_hello.txt
	./dimon-as examples/iso_boot.asm -o examples/iso_boot.bin
	./dimon-as examples/disk_demo.asm -o examples/disk_demo.bin
	./dimon-mkiso -b examples/iso_boot.bin -o dimon.iso examples/iso_hello.txt:README examples/hello.bin:HELLO
	./dimon-mkiso --list dimon.iso

iso: dimon.iso

iso-test: iso
	@echo "--- boot from ISO (sector 0) ---"
	./dimon-emu --iso dimon.iso
	@echo "--- program + disk (disk_demo + ISO) ---"
	./dimon-emu examples/disk_demo.bin --iso dimon.iso

# Native Bare-Metal x86 Kernel and Standalone ISO
arch/x86/boot.o: arch/x86/boot.S os.bin dimon.iso
	$(BM_CC) -c -o arch/x86/boot.o arch/x86/boot.S

arch/x86/kernel.o: arch/x86/kernel.c arch/x86/io.h dimon16.h
	$(BM_CC) $(BM_CFLAGS) -c -o arch/x86/kernel.o arch/x86/kernel.c

vm_baremetal.o: vm.c dimon16.h
	$(BM_CC) $(BM_CFLAGS) -c -o vm_baremetal.o vm.c

baremetal-kernel: dimon-kernel.elf

dimon-kernel.elf: arch/x86/boot.o arch/x86/kernel.o vm_baremetal.o arch/x86/linker.ld
	$(BM_LD) -T arch/x86/linker.ld -nostdlib -o dimon-kernel.elf arch/x86/boot.o arch/x86/kernel.o vm_baremetal.o

baremetal-iso: dimon-baremetal.iso

dimon-baremetal.iso: dimon-kernel.elf arch/x86/grub.cfg
	mkdir -p build/iso/boot/grub
	cp dimon-kernel.elf build/iso/boot/dimon-kernel.elf
	cp arch/x86/grub.cfg build/iso/boot/grub/grub.cfg
	grub-mkrescue -o dimon-baremetal.iso build/iso

qemu: dimon-baremetal.iso
	qemu-system-i386 -cdrom dimon-baremetal.iso

clean:
	rm -rf dimon-as dimon-emu dimon-mkiso os.bin dimon.iso examples/*.bin
	rm -rf dimon-kernel.elf dimon-baremetal.iso build arch/x86/*.o vm_baremetal.o *.ppm *.log

.PHONY: all progs test gui-test iso iso-test baremetal-kernel baremetal-iso qemu clean
