# DimonOS native bare-metal build
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2
BM_CC   = gcc -m32
BM_CFLAGS = -std=c11 -Wall -Wextra -O2 -ffreestanding -nostdlib -DBAREMETAL -fno-pie -fno-stack-protector -I.
BM_LD   = ld -m elf_i386 -z noexecstack

REG_BUILD ?= build/regression
REG_DISK  = $(REG_BUILD)/test-disk.img
REG_KERNEL= $(REG_BUILD)/dimon-kernel.elf
REG_ISO   = $(REG_BUILD)/dimon-regression.iso
DATA_DISK ?= dimon-data.img

ALL = dimon-mkiso dimon-disk.img dimon-kernel.elf dimon-baremetal.iso

all: $(ALL)

# ---- Host tool: FAT16 image builder (no VM dependency) ----
dimon-mkiso: mkiso.c dimonfs.c dimonfs.h dimon64.h
	$(CC) $(CFLAGS) -o dimon-mkiso mkiso.c dimonfs.c

# ---- Native kernel ----
kernel_runtime_baremetal.o: kernel_runtime.c kernel_runtime.h dimon64.h dimonfs.h font8x16.h
	$(BM_CC) $(BM_CFLAGS) -c -o $@ kernel_runtime.c

desktop_baremetal.o: desktop.c desktop.h kernel_runtime.h dimon64.h dimonfs.h napp.h
	$(BM_CC) $(BM_CFLAGS) -c -o $@ desktop.c

dimonfs_baremetal.o: dimonfs.c dimonfs.h dimon64.h
	$(BM_CC) $(BM_CFLAGS) -c -o $@ dimonfs.c

arch/x86/ata.o: arch/x86/ata.c arch/x86/ata.h arch/x86/io.h dimon64.h
	$(BM_CC) $(BM_CFLAGS) -c -o $@ arch/x86/ata.c

arch/x86/kernel.o: arch/x86/kernel.c arch/x86/io.h arch/x86/ata.h dimon64.h desktop.h kernel_runtime.h
	$(BM_CC) $(BM_CFLAGS) -c -o $@ arch/x86/kernel.c

arch/x86/boot.o: arch/x86/boot.S dimon-disk.img
	$(BM_CC) -DDISK_IMAGE_PATH='"dimon-disk.img"' -c -o arch/x86/boot.o arch/x86/boot.S

dimon-kernel.elf: arch/x86/boot.o arch/x86/kernel.o arch/x86/ata.o \
                  desktop_baremetal.o kernel_runtime_baremetal.o dimonfs_baremetal.o arch/x86/linker.ld
	$(BM_LD) -T arch/x86/linker.ld -nostdlib -o dimon-kernel.elf \
	  arch/x86/boot.o arch/x86/kernel.o arch/x86/ata.o \
	  desktop_baremetal.o kernel_runtime_baremetal.o dimonfs_baremetal.o

# ---- Raw FAT16 disk image with native Snake app (not an ISO) ----
build/apps/SNAKE.APP: apps/snake.c napp.h
	mkdir -p build/apps
	$(BM_CC) -ffreestanding -nostdlib -fno-pic -fno-stack-protector -O2 -c apps/snake.c -o build/snake_app.o
	$(BM_LD) -Ttext=0 -e app_main --oformat binary -o build/snake_app.bin build/snake_app.o
	python3 tools/mknapp.py build/snake_app.o build/snake_app.bin build/apps/SNAKE.APP SNAKE

apps: build/apps/SNAKE.APP

dimon-disk.img: dimon-mkiso apps
	./dimon-mkiso -o dimon-disk.img build/apps/SNAKE.APP:SNAKE.APP --force

# Backward-compatibility alias (historical name; this is a raw FAT16 image).
dimon.iso: dimon-disk.img
	@cp -f dimon-disk.img dimon.iso

disk-new: dimon-disk.img

# ---- Bootable ISO (GRUB + kernel) ----
dimon-baremetal.iso: dimon-kernel.elf arch/x86/grub.cfg
	mkdir -p build/iso/boot/grub
	cp dimon-kernel.elf build/iso/boot/dimon-kernel.elf
	cp arch/x86/grub.cfg build/iso/boot/grub/grub.cfg
	grub-mkrescue -o dimon-baremetal.iso build/iso

baremetal: dimon-kernel.elf
baremetal-iso: dimon-baremetal.iso

# ---- QEMU / VirtualBox ----
qemu: dimon-baremetal.iso
	qemu-system-i386 -cdrom dimon-baremetal.iso -boot d -m 256M -serial stdio -vga std

run-qemu-persistent: dimon-baremetal.iso dimon-disk.img
	qemu-system-i386 -cdrom dimon-baremetal.iso -drive file=dimon-disk.img,format=raw,index=0,media=disk,file.locking=off -boot d -m 256M -serial stdio -vga std

kill-qemu:
	pkill -9 -f qemu-system-i386 || true

data-disk-new: dimon-mkiso apps
	./dimon-mkiso -o $(DATA_DISK) build/apps/SNAKE.APP:SNAKE.APP

data-disk-new-force: dimon-mkiso apps
	./dimon-mkiso -o $(DATA_DISK) build/apps/SNAKE.APP:SNAKE.APP --force

data-disk-list: dimon-mkiso
	./dimon-mkiso --list $(DATA_DISK)

# ---- Tests ----
test: dimon-mkiso apps dimon-disk.img
	python3 tests/fs_unit_test.py
	@echo "=== FS unit tests passed ==="

regression: test regression-qemu

$(REG_BUILD):
	mkdir -p $(REG_BUILD)

$(REG_DISK): dimon-mkiso apps | $(REG_BUILD)
	mkdir -p $(REG_BUILD)/fixtures
	python3 -c "open('$(REG_BUILD)/fixtures/LARGE.TXT','wb').write(b'A'*4500); open('$(REG_BUILD)/fixtures/BADENC.BIN','wb').write(bytes([0xFF, 0xFE, 0xFD, 0xFC, 0x80, 0x81, 0xC0, 0xAF, 0xE0, 0x80])); open('$(REG_BUILD)/fixtures/sub_notes.txt','w').write('Subdirectory notes content\n')"
	./dimon-mkiso -o $@ build/apps/SNAKE.APP:SNAKE.APP $(REG_BUILD)/fixtures/LARGE.TXT:LARGE.TXT $(REG_BUILD)/fixtures/BADENC.BIN:BADENC.BIN --force > $(REG_BUILD)/mkiso.log
	./dimon-mkiso --mkdir $@ /DOCS >> $(REG_BUILD)/mkiso.log
	./dimon-mkiso --add $@ $(REG_BUILD)/fixtures/sub_notes.txt:/DOCS/NOTES.TXT >> $(REG_BUILD)/mkiso.log

$(REG_BUILD)/boot.o: arch/x86/boot.S $(REG_DISK) | $(REG_BUILD)
	$(BM_CC) -DDISK_IMAGE_PATH='"$(abspath $(REG_DISK))"' -c -o $@ arch/x86/boot.S

$(REG_BUILD)/kernel.o: arch/x86/kernel.c | $(REG_BUILD)
	$(BM_CC) $(BM_CFLAGS) -c -o $@ arch/x86/kernel.c

$(REG_BUILD)/ata.o: arch/x86/ata.c | $(REG_BUILD)
	$(BM_CC) $(BM_CFLAGS) -c -o $@ arch/x86/ata.c

$(REG_BUILD)/desktop.o: desktop.c | $(REG_BUILD)
	$(BM_CC) $(BM_CFLAGS) -c -o $@ desktop.c

$(REG_BUILD)/kernel_runtime.o: kernel_runtime.c | $(REG_BUILD)
	$(BM_CC) $(BM_CFLAGS) -c -o $@ kernel_runtime.c

$(REG_BUILD)/dimonfs.o: dimonfs.c | $(REG_BUILD)
	$(BM_CC) $(BM_CFLAGS) -c -o $@ dimonfs.c

$(REG_KERNEL): $(REG_BUILD)/boot.o $(REG_BUILD)/kernel.o $(REG_BUILD)/ata.o \
               $(REG_BUILD)/desktop.o $(REG_BUILD)/kernel_runtime.o $(REG_BUILD)/dimonfs.o arch/x86/linker.ld
	$(BM_LD) -T arch/x86/linker.ld -nostdlib -o $@ \
	  $(REG_BUILD)/boot.o $(REG_BUILD)/kernel.o $(REG_BUILD)/ata.o \
	  $(REG_BUILD)/desktop.o $(REG_BUILD)/kernel_runtime.o $(REG_BUILD)/dimonfs.o

$(REG_ISO): $(REG_KERNEL) arch/x86/grub.cfg
	mkdir -p $(REG_BUILD)/iso/boot/grub
	cp $(REG_KERNEL) $(REG_BUILD)/iso/boot/dimon-kernel.elf
	cp arch/x86/grub.cfg $(REG_BUILD)/iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(REG_BUILD)/iso > $(REG_BUILD)/grub.log 2>&1

regression-artifacts: $(REG_DISK) $(REG_ISO)

regression-qemu: regression-artifacts
	python3 tests/qemu_regression.py --build-dir $(REG_BUILD)

regression-persistent: regression-artifacts
	python3 tests/qemu_persistent_regression.py --build-dir $(REG_BUILD)

clean:
	rm -f -- dimon-mkiso dimon-kernel.elf dimon-baremetal.iso dimon-disk.img dimon.iso
	rm -f -- *.o arch/x86/*.o
	rm -f -- *.ppm *.log
	rm -rf -- build "$(REG_BUILD)" tests/__pycache__

help:
	@echo "Targets:"
	@echo "  make all               kernel ELF + mkiso host tool"
	@echo "  make dimon-disk.img    raw FAT16 disk image with native SNAKE.APP"
	@echo "  make dimon.iso         alias for dimon-disk.img (backward compat)"
	@echo "  make dimon-baremetal.iso  bootable GRUB ISO-9660"
	@echo "  make qemu              boot in QEMU (VGA/VBE, PC speaker, serial)"
	@echo "  make kill-qemu         terminate any running qemu-system-i386"
	@echo "  make test              FAT16 unit tests"
	@echo "  make regression        unit tests + QEMU regressions"
	@echo "  make data-disk-new     writable FAT16 data disk for persistence"
	@echo "  make run-qemu-persistent  boot ISO + IDE data disk"

.PHONY: all apps disk-new baremetal baremetal-iso qemu kill-qemu run-qemu-persistent \
        test regression regression-artifacts regression-qemu regression-persistent \
        data-disk-new data-disk-new-force data-disk-list clean help
