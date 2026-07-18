.code64
.section .text

.global long_mode_entry
.extern kernel_main

long_mode_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    movabs $stack_top, %rsp

    popq %rax
    popq %rbx
    movq %rbx, %rdi
    call kernel_main

halt:
    hlt
    jmp halt
