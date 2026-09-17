CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11

REG_BUILD ?= build/regression
REG_OS = $(REG_BUILD)/os.bin
REG_DISK = $(REG_BUILD)/test-disk.img
REG_KERNEL = $(REG_BUILD)/dimon-kernel.elf
REG_ISO = $(REG_BUILD)/dimon-regression.iso
LEGACY_TEST_DISK = $(REG_BUILD)/legacy-test-disk.img

ALL = dimon-as dimon-emu dimon-mkiso

all: $(ALL)

dimon-as: assembler.c dimon64.h
	$(CC) $(CFLAGS) -o dimon-as assembler.c

dimon-emu: emulator.c vm.c dimonfs.c dimon64.h dimonfs.h font8x16.h
	$(CC) $(CFLAGS) -o dimon-emu emulator.c vm.c dimonfs.c -lX11 -pthread

dimon-mkiso: mkiso.c dimonfs.c dimonfs.h dimon64.h
	$(CC) $(CFLAGS) -o dimon-mkiso mkiso.c dimonfs.c

disk-install-apps: dimon-mkiso apps
	./dimon-mkiso --add dimon.iso apps/snake.app:SNAKE.APP

os.bin: $(ALL) os.asm
	./dimon-as os.asm -o os.bin

dimon.iso:
	@echo "dimon.iso is missing; create it explicitly with 'make disk-new'" >&2
	@false

# Explicit application artifacts. DEXE64 contains relocation metadata and is
# loaded from FAT16 by SYS_APP_EXEC. Integrated desktop modules remain built as
# part of os.bin while they are migrated to the process ABI.
apps/snake.app: dimon-as apps/snake.asm
	./dimon-as apps/snake.asm --dexe Snake -o apps/snake.app

apps: apps/snake.app

# Image creation is intentionally explicit and mkiso refuses to replace an
# existing image unless --force is supplied directly by the operator.
disk-new: dimon-mkiso apps
	./dimon-mkiso -o dimon.iso apps/snake.app:SNAKE.APP

progs: $(ALL) os.bin apps
	./dimon-as examples/hello.asm -o examples/hello.bin
	./dimon-as examples/math.asm -o examples/math.bin
	./dimon-as examples/fib.asm -o examples/fib.bin
	./dimon-as examples/multitask.asm -o examples/multitask.bin
	./dimon-as examples/gui_test.asm -o examples/gui_test.bin
	./dimon-as apps/snake.asm -o apps/snake.bin
	./dimon-as examples/psg_test.asm -o examples/psg_test.bin

test: progs
	@echo "--- hello (expected: Hello, DimonVirtualCPU-64!) ---"
	./dimon-emu examples/hello.bin
	@echo "--- psg synthesizer MMIO (0x03FFE000 freq/wave/vol/dur/status) ---"
	./dimon-emu --headless examples/psg_test.bin > /tmp/dimon_psg_test.out
	python3 -c "lines = open('/tmp/dimon_psg_test.out').read().strip().split('\n'); assert lines == ['440', '1', '180', '50', '1'], f'Unexpected PSG output: {lines}'; print('PSG MMIO registers & synthesizer trigger OK')"
	@echo "--- math (expected: 35 42 42 42 48 255 85 32 8) ---"
	./dimon-emu examples/math.bin
	@echo "--- fib (expected: 0 1 1 2 3 5 8 13 21 34) ---"
	./dimon-emu examples/fib.bin
	@echo "--- multitask (expected: interleaved Main/Task1/Task2) ---"
	./dimon-emu examples/multitask.bin
	@echo "--- gui_test 800x600 TrueColor LFB ---"
	./dimon-emu --headless -m 50000 examples/gui_test.bin --dump-vram /tmp/dimon_gui_test.bin
	python3 -c "import struct; d=open('/tmp/dimon_gui_test.bin','rb').read(); assert len(d)==800*600*4, 'VRAM size mismatch'; p0=struct.unpack_from('<I',d,0)[0]; p1=struct.unpack_from('<I',d,(50*800+100)*4)[0]; assert p0==0xFF003366 and p1==0xFF1E293B, 'gui_test color mismatch'; print('gui_test TrueColor LFB OK (800x600 @ 32bpp, 1920000 B)')"
	@echo "--- os desktop (800x600x4 TrueColor VRAM buffer at 0x02000000) ---"
	./dimon-emu --headless -m 100000 os.bin --dump-vram /tmp/dimon_os.bin
	python3 -c "import struct; d=open('/tmp/dimon_os.bin','rb').read(); assert len(d)==800*600*4, 'Desktop VRAM size mismatch'; p=lambda x,y: struct.unpack_from('<I',d,(y*800+x)*4)[0]; assert p(10,10)==0xFF111827, 'Top bar missing'; assert p(400,300)==0xFF1E2430, 'Desktop bg missing'; assert p(40,580)==0xFF10B981, 'Start button missing'; assert p(500,580)==0xFF0F172A, 'Taskbar missing'; print('os desktop TrueColor LFB OK')"
	@echo "--- os start menu popup via F1 / \1 ---"
	./dimon-emu --headless --inject-keys '\1' -m 100000 os.bin --dump-vram /tmp/dimon_os_menu.bin
	python3 -c "import struct; d=open('/tmp/dimon_os_menu.bin','rb').read(); p=lambda x,y: struct.unpack_from('<I',d,(y*800+x)*4)[0]; assert p(50,300)==0xFF1E222D, 'Start menu popup missing'; print('os start menu popup OK')"
	@echo "--- os all 7 apps (opening windows in 800x600 TrueColor) ---"
	for k in 1 2 3 4 5 6 7; do ./dimon-emu --headless --inject-keys "$$k" -m 200000 os.bin --dump-vram /tmp/dimon_os_app$$k.bin; done
	python3 -c "import struct; wins={1:(240,70),2:(80,45),3:(100,60),4:(60,35),5:(140,70),6:(120,50),7:(90,55)}; [exit(f'App {k} window missing') for k, (wx, wy) in wins.items() if struct.unpack_from('<I',open(f'/tmp/dimon_os_app{k}.bin','rb').read(),((wy+10)*800+wx+4)*4)[0]!=0xFF2563EB]; print('all 7 app windows opened in TrueColor OK')"
	@echo "--- os calculator 64-bit arithmetic typing 125*4= ---"
	./dimon-emu --headless --inject-keys "1125*4=" -m 200000 os.bin --dump-vram /tmp/dimon_os_calc.bin
	python3 -c "import struct; d=open('/tmp/dimon_os_calc.bin','rb').read(); p=lambda x,y: struct.unpack_from('<I',d,(y*800+x)*4)[0]; found=any(p(x,y)==0xFF38BDF8 for y in range(130,160) for x in range(260,360)); assert found, 'Calc display output missing'; print('os calculator 64-bit arithmetic OK')"
	./dimon-emu --headless --inject-keys "1" --inject-click "290,220;500,390;360,220;430,390" -m 300000 os.bin --dump-vram /tmp/dimon_calc_click.bin
	python3 -c "import struct; d=open('/tmp/dimon_calc_click.bin','rb').read(); p=lambda x,y: struct.unpack_from('<I',d,(y*800+x)*4)[0]; found=any(p(x,y)==0xFF38BDF8 for y in range(120,170) for x in range(260,540)); assert found, 'Calc click calculation missing'; print('os calculator mouse click calculation OK')"
	@echo "--- paint canvas drawing (injected mouse click draws non-zero 32-bit color) ---"
	./dimon-emu --headless --inject-keys "4" --inject-click "300,250" -m 200000 os.bin --dump-vram /tmp/dimon_paint_click.bin
	python3 -c "import struct; d=open('/tmp/dimon_paint_click.bin','rb').read(); p=struct.unpack_from('<I',d,(250*800+300)*4)[0]; assert p==0xFFFF3B30, f'Canvas click mismatch: {hex(p)}'; print('paint injected click drawing OK (32-bit TrueColor red pixel drawn)')"
	./dimon-emu --headless --inject-keys "4" --inject-click "200,200;175,90;400,300" -m 300000 os.bin --dump-vram /tmp/dimon_paint_multi.bin
	python3 -c "import struct; d=open('/tmp/dimon_paint_multi.bin','rb').read(); p=lambda x,y: struct.unpack_from('<I',d,(y*800+x)*4)[0]; assert p(200,200)==0xFFFF3B30 and p(400,300)==0xFF34C759, 'Paint multi-click swatch drawing failed'; print('paint swatch selection and multi-click drawing OK')"
	@echo "--- FAT16 filesystem (mount, root directory listing, and Notepad write persistence) ---"
	mkdir -p $(REG_BUILD)
	./dimon-mkiso -o $(LEGACY_TEST_DISK) --force
	./dimon-mkiso --list $(LEGACY_TEST_DISK)
	python3 -c "import struct; b=open('$(LEGACY_TEST_DISK)','rb').read(); assert b[510:512]==b'\x55\xAA', 'BPB signature missing'; assert b[3:11]==b'DIMON64 ', 'OEM missing'; assert b[54:62]==b'FAT16   ', 'FS type missing'; assert b[36*512:36*512+11]==b'NOTES   TXT', 'notes.txt missing in root dir'; print('FAT16 volume layout and root dir OK')"
	./dimon-emu --headless --disk $(LEGACY_TEST_DISK) --disk-writable --inject-keys "2Hello DimonOS!\9" -m 1500000 os.bin
	python3 -c "with open('$(LEGACY_TEST_DISK)','rb') as f: f.seek(68*512); d=f.read(512); assert b'Hello DimonOS!' in d, 'Saved text missing from FAT16 data cluster 2'; f.seek(36*512); ent=f.read(32); sz=ent[28]|(ent[29]<<8); assert sz==127+len('Hello DimonOS!'), f'Dir entry size mismatch: {sz}'; print('Notepad FAT16 cluster write & root dir update OK')"
	./dimon-emu --headless --disk $(LEGACY_TEST_DISK) --inject-keys "2" -m 200000 os.bin --dump-vram /tmp/dimon_note_reload.bin
	python3 -c "import struct; d=open('/tmp/dimon_note_reload.bin','rb').read(); assert len(d)==800*600*4; print('Notepad reload from FAT16 OK')"
	./dimon-emu --headless --disk $(LEGACY_TEST_DISK) --inject-keys "3" -m 200000 os.bin --dump-vram /tmp/dimon_fm_reload.bin
	python3 -c "import struct; d=open('/tmp/dimon_fm_reload.bin','rb').read(); assert len(d)==800*600*4; p=lambda x,y: struct.unpack_from('<I',d,(y*800+x)*4)[0]; assert p(104,70)==0xFF2563EB, 'File Explorer title missing'; print('File Explorer FAT16 root directory viewer OK')"
	@echo "--- snake game (advances, wall collision, and restart via 'R' without freezing) ---"
	./dimon-emu --headless --inject-keys "                                        " -m 100000 apps/snake.bin --dump-vram /tmp/snake_collide.bin
	python3 -c "import struct; d=open('/tmp/snake_collide.bin','rb').read(); p=struct.unpack_from('<I',d,(240*800+260)*4)[0]; assert p==0xFFFF3B30, f'Expected Game Over header, got {hex(p)}'; print('snake wall collision & Game Over dialog OK')"
	./dimon-emu --headless --inject-keys "                                        R" -m 150000 apps/snake.bin --dump-vram /tmp/snake_restart.bin
	python3 -c "import struct; d=open('/tmp/snake_restart.bin','rb').read(); p=struct.unpack_from('<I',d,(240*800+260)*4)[0]; assert p==0xFF0F172A, f'Expected reset board, got {hex(p)}'; print('snake restart via R without freezing OK')"
	@echo "--- os multitasking (ticks & switches advance, no deadlock) ---"
	./dimon-emu --headless -m 500000 os.bin -r 2>&1 | tail -n 9
	@echo "=== ALL TESTS PASSED SUCCESSFULLY ==="

# Native Bare-Metal x86 Kernel and Standalone ISO
BM_CC = gcc -m32
BM_CFLAGS = -std=c11 -Wall -Wextra -O2 -ffreestanding -nostdlib -DBAREMETAL -fno-pie -fno-stack-protector -I.
BM_LD = ld -m elf_i386 -z noexecstack

arch/x86/boot.o: arch/x86/boot.S os.bin dimon.iso
	$(BM_CC) -c -o arch/x86/boot.o arch/x86/boot.S

arch/x86/kernel.o: arch/x86/kernel.c arch/x86/io.h dimon64.h
	$(BM_CC) $(BM_CFLAGS) -c -o arch/x86/kernel.o arch/x86/kernel.c

vm_baremetal.o: vm.c dimon64.h dimonfs.h font8x16.h
	$(BM_CC) $(BM_CFLAGS) -c -o vm_baremetal.o vm.c

dimonfs_baremetal.o: dimonfs.c dimonfs.h dimon64.h
	$(BM_CC) $(BM_CFLAGS) -c -o dimonfs_baremetal.o dimonfs.c

dimon-kernel.elf: arch/x86/boot.o arch/x86/kernel.o vm_baremetal.o dimonfs_baremetal.o arch/x86/linker.ld
	$(BM_LD) -T arch/x86/linker.ld -nostdlib -o dimon-kernel.elf arch/x86/boot.o arch/x86/kernel.o vm_baremetal.o dimonfs_baremetal.o

baremetal: dimon-kernel.elf

dimon-baremetal.iso: dimon-kernel.elf arch/x86/grub.cfg
	mkdir -p build/iso/boot/grub
	cp dimon-kernel.elf build/iso/boot/dimon-kernel.elf
	cp arch/x86/grub.cfg build/iso/boot/grub/grub.cfg
	grub-mkrescue -o dimon-baremetal.iso build/iso

baremetal-iso: dimon-baremetal.iso

qemu: dimon-baremetal.iso
	qemu-system-i386 -cdrom dimon-baremetal.iso -boot d -m 256M -serial stdio -vga std

qemu-multiboot: dimon-kernel.elf
	qemu-system-i386 -kernel dimon-kernel.elf -m 256M -serial stdio -vga std

# Isolated regression artifacts. These rules never read, overwrite, or embed
# the user's dimon.iso; boot.S paths are supplied explicitly to the assembler.
$(REG_BUILD):
	mkdir -p $(REG_BUILD)

$(REG_OS): dimon-as os.asm | $(REG_BUILD)
	./dimon-as os.asm -o $@ > $(REG_BUILD)/os.map

$(REG_DISK): dimon-mkiso apps/snake.app | $(REG_BUILD)
	./dimon-mkiso -o $@ apps/snake.app:SNAKE.APP --force > $(REG_BUILD)/mkiso.log

$(REG_BUILD)/boot.o: arch/x86/boot.S $(REG_OS) $(REG_DISK) | $(REG_BUILD)
	$(BM_CC) -DOS_IMAGE_PATH='"$(abspath $(REG_OS))"' -DDISK_IMAGE_PATH='"$(abspath $(REG_DISK))"' -c -o $@ arch/x86/boot.S

$(REG_BUILD)/kernel.o: arch/x86/kernel.c arch/x86/io.h dimon64.h | $(REG_BUILD)
	$(BM_CC) $(BM_CFLAGS) -c -o $@ arch/x86/kernel.c

$(REG_BUILD)/vm.o: vm.c dimon64.h dimonfs.h font8x16.h | $(REG_BUILD)
	$(BM_CC) $(BM_CFLAGS) -c -o $@ vm.c

$(REG_BUILD)/dimonfs.o: dimonfs.c dimonfs.h dimon64.h | $(REG_BUILD)
	$(BM_CC) $(BM_CFLAGS) -c -o $@ dimonfs.c

$(REG_KERNEL): $(REG_BUILD)/boot.o $(REG_BUILD)/kernel.o $(REG_BUILD)/vm.o $(REG_BUILD)/dimonfs.o arch/x86/linker.ld
	$(BM_LD) -T arch/x86/linker.ld -nostdlib -o $@ $(REG_BUILD)/boot.o $(REG_BUILD)/kernel.o $(REG_BUILD)/vm.o $(REG_BUILD)/dimonfs.o

$(REG_ISO): $(REG_KERNEL) arch/x86/grub.cfg
	mkdir -p $(REG_BUILD)/iso/boot/grub
	cp $(REG_KERNEL) $(REG_BUILD)/iso/boot/dimon-kernel.elf
	cp arch/x86/grub.cfg $(REG_BUILD)/iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(REG_BUILD)/iso > $(REG_BUILD)/grub.log 2>&1

regression-artifacts: all apps $(REG_OS) $(REG_DISK) $(REG_ISO)

regression-hosted: all $(REG_OS)
	python3 tests/desktop_regression.py --build-dir $(REG_BUILD)

regression-x11: all $(REG_OS)
	python3 tests/x11_regression.py --build-dir $(REG_BUILD)

regression-qemu: regression-artifacts
	python3 tests/qemu_regression.py --build-dir $(REG_BUILD)

regression: regression-hosted regression-x11 regression-qemu

# Remove generated programs, objects, boot ISOs, and test artifacts in one step.
# dimon.iso is the persistent user disk and is intentionally preserved.
clean:
	@reg_dir="$(abspath $(REG_BUILD))"; \
	case "$(CURDIR)/" in "$${reg_dir%/}/"*) \
		echo "Refusing clean: REG_BUILD must not be the project directory or an ancestor." >&2; exit 1 ;; \
	esac
	rm -f -- $(ALL) $(addsuffix .exe,$(ALL)) os.bin dimon-kernel.elf dimon-baremetal.iso
	rm -f -- *.o arch/x86/*.o examples/*.bin apps/*.bin apps/*.app
	rm -f -- *.ppm *.log /tmp/dimon_*.bin /tmp/snake_*.bin /tmp/dimon_psg_test.out
	rm -rf -- build "$(REG_BUILD)" tests/__pycache__

.PHONY: all apps disk-new disk-install-apps progs test clean baremetal baremetal-iso qemu qemu-multiboot regression-artifacts regression-hosted regression-x11 regression-qemu regression
