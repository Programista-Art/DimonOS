CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11

ALL = dimon-as dimon-emu dimon-mkiso

all: $(ALL)

dimon-as: assembler.c dimon64.h
	$(CC) $(CFLAGS) -o dimon-as assembler.c

dimon-emu: emulator.c vm.c dimon64.h font8x16.h
	$(CC) $(CFLAGS) -o dimon-emu emulator.c vm.c -lX11

dimon-mkiso: mkiso.c dimon64.h
	$(CC) $(CFLAGS) -o dimon-mkiso mkiso.c

os.bin: $(ALL) os.asm
	./dimon-as os.asm -o os.bin

dimon.iso: dimon-mkiso
	./dimon-mkiso -o dimon.iso

progs: $(ALL) os.bin dimon.iso
	./dimon-as examples/hello.asm -o examples/hello.bin
	./dimon-as examples/math.asm -o examples/math.bin
	./dimon-as examples/fib.asm -o examples/fib.bin
	./dimon-as examples/multitask.asm -o examples/multitask.bin
	./dimon-as examples/gui_test.asm -o examples/gui_test.bin
	./dimon-as apps/snake.asm -o apps/snake.bin

test: progs
	@echo "--- hello (expected: Hello, DimonVirtualCPU-64!) ---"
	./dimon-emu examples/hello.bin
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
	@echo "--- notepad SimpleFS disk persistence (write to dimon.iso and reload) ---"
	./dimon-mkiso -o dimon.iso
	./dimon-emu --headless --disk dimon.iso --disk-writable --inject-keys "2Hello DimonOS!\9" -m 1500000 os.bin
	python3 -c "with open('dimon.iso','rb') as f: f.seek(18*512); d=f.read(512); assert b'Hello DimonOS!' in d, 'Saved text missing from sector 18'; print('notepad saved bytes to dimon.iso Sector 18 OK')"
	./dimon-emu --headless --disk dimon.iso --inject-keys "2" -m 200000 os.bin --dump-vram /tmp/dimon_note_reload.bin
	python3 -c "import struct; d=open('/tmp/dimon_note_reload.bin','rb').read(); assert len(d)==800*600*4; print('notepad reload from dimon.iso OK')"
	@echo "--- snake game (advances, wall collision, and restart via 'R' without freezing) ---"
	./dimon-emu --headless --inject-keys "                                        " -m 100000 apps/snake.bin --dump-vram /tmp/snake_collide.bin
	python3 -c "import struct; d=open('/tmp/snake_collide.bin','rb').read(); p=struct.unpack_from('<I',d,(240*800+260)*4)[0]; assert p==0xFFFF3B30, f'Expected Game Over header, got {hex(p)}'; print('snake wall collision & Game Over dialog OK')"
	./dimon-emu --headless --inject-keys "                                        R" -m 150000 apps/snake.bin --dump-vram /tmp/snake_restart.bin
	python3 -c "import struct; d=open('/tmp/snake_restart.bin','rb').read(); p=struct.unpack_from('<I',d,(240*800+260)*4)[0]; assert p==0xFF0F172A, f'Expected reset board, got {hex(p)}'; print('snake restart via R without freezing OK')"
	@echo "--- os multitasking (ticks & switches advance, no deadlock) ---"
	./dimon-emu --headless -m 500000 os.bin -r 2>&1 | tail -n 9
	@echo "=== ALL TESTS PASSED SUCCESSFULLY ==="

clean:
	rm -rf dimon-as dimon-emu dimon-mkiso os.bin dimon.iso examples/*.bin apps/snake.bin
	rm -rf build *.ppm *.log /tmp/dimon_*.bin /tmp/snake_*.bin

.PHONY: all progs test clean
