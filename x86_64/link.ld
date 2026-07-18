OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS {
    . = 0x100000;

    .text : ALIGN(4K) {
        *(.text.boot)
        *(.text*)
    }

    .ltext : ALIGN(4K) {
        *(.ltext*)
    }

    .rodata : ALIGN(4K) {
        *(.rodata*)
    }

    .data : ALIGN(4K) {
        *(.data*)
    }

    .bss : ALIGN(4K) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    .lbss : ALIGN(4K) {
        *(.lbss*)
        *(COMMON)
    }

    .ldata : ALIGN(4K) {
        *(.ldata*)
    }

    __end = .;
}
