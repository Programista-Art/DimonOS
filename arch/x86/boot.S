# Multiboot 1 header constants
.set MB_ALIGN,     1 << 0
.set MB_MEMINFO,   1 << 1
.set MB_VIDEOMODE, 1 << 2
.set MB_FLAGS,     MB_ALIGN | MB_MEMINFO | MB_VIDEOMODE
.set MB_MAGIC,     0x1BADB002
.set MB_CHECKSUM,  -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM

# A.OUT kludge fields (offsets 12..28, unused for ELF, set to 0)
.long 0   # header_addr
.long 0   # load_addr
.long 0   # load_end_addr
.long 0   # bss_end_addr
.long 0   # entry_addr

# Video mode preference fields (offsets 32..44, valid because bit 2 is set)
.long 0   # mode_type: 0 = linear framebuffer
.long 800 # width
.long 600 # height
.long 32  # depth (bpp)

# Global Descriptor Table (GDT)
.section .data
.align 16
gdt_start:
    # 0x00: Null descriptor
    .quad 0

    # 0x08: Kernel 32-bit Code Segment (Base=0, Limit=4GB, Ring 0, Exec/Read)
    .word 0xFFFF, 0x0000
    .byte 0x00, 0x9A, 0xCF, 0x00

    # 0x10: Kernel 32-bit Data Segment (Base=0, Limit=4GB, Ring 0, Read/Write)
    .word 0xFFFF, 0x0000
    .byte 0x00, 0x92, 0xCF, 0x00
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    cli
    # Preserve Multiboot magic (EAX) and info pointer (EBX)
    mov %eax, %edi
    mov %ebx, %esi

    # Load known GDT and reload segments
    lgdt gdt_descriptor
    ljmp $0x08, $1f
1:
    mov $0x10, %cx
    mov %cx, %ds
    mov %cx, %es
    mov %cx, %fs
    mov %cx, %gs
    mov %cx, %ss
    mov $stack_top, %esp

    # Call kernel_main(uint32_t magic, uint32_t mb_info)
    push %esi           # Multiboot info pointer
    push %edi           # Multiboot magic
    call kernel_main

    cli
2:
    hlt
    jmp 2b

# --- Interrupt Handlers / ISR Stubs ---
.extern irq0_c_handler
.extern irq1_c_handler
.extern irq12_c_handler
.extern default_isr_c_handler

.global irq0_stub
.type irq0_stub, @function
irq0_stub:
    pushal
    cld
    call irq0_c_handler
    popal
    iretl

.global irq1_stub
.type irq1_stub, @function
irq1_stub:
    pushal
    cld
    call irq1_c_handler
    popal
    iretl

.global irq12_stub
.type irq12_stub, @function
irq12_stub:
    pushal
    cld
    call irq12_c_handler
    popal
    iretl

.global default_isr_stub
.type default_isr_stub, @function
default_isr_stub:
    pushal
    cld
    call default_isr_c_handler
    popal
    iretl

# --- Embedded Assets ---
.section .rodata
.align 4
.global _os_bin_start
.global _os_bin_end
_os_bin_start:
    .incbin "os.bin"
_os_bin_end:

.align 4
.global _dimon_iso_start
.global _dimon_iso_end
_dimon_iso_start:
    .incbin "dimon.iso"
_dimon_iso_end:

.section .note.GNU-stack,"",@progbits
