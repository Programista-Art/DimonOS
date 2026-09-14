ENTRY(_start)
OUTPUT_FORMAT("elf32-i386")

SECTIONS {
    . = 1M;

    .multiboot : ALIGN(4) {
        KEEP(*(.multiboot))
    }

    .text : ALIGN(4K) {
        *(.text*)
    }

    .rodata : ALIGN(4K) {
        *(.rodata*)
    }

    .data : ALIGN(4K) {
        *(.data*)
    }

    .bss : ALIGN(4K) {
        *(COMMON)
        *(.bss*)
        . = ALIGN(4K);
    }

    _kernel_end = .;
}
