.code32
.section .text.boot, "ax"

.set MULTIBOOT2_MAGIC, 0xE85250D6
.set MULTIBOOT2_ARCH, 0

.align 8
multiboot_start:
    .long MULTIBOOT2_MAGIC
    .long MULTIBOOT2_ARCH
    .long multiboot_end - multiboot_start
    .long -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + (multiboot_end - multiboot_start))

    .align 8
    .word 5
    .word 0
    .long 20
    .long 1024
    .long 768
    .long 32

    .align 8
    .word 0
    .word 0
    .long 8
multiboot_end:

.global _start
.extern long_mode_entry

_start:
    cli
    movl $stack_top, %esp
    movl %eax, mb_magic
    movl %ebx, mb_info

    call check_cpuid
    call check_long_mode

    leal page_table_l4, %edi
    movl %edi, %cr3

    movl $0x200, %ecx
    xorl %eax, %eax
    rep stosl

    leal page_table_l4, %edi
    leal page_table_l3, %eax
    orl $3, %eax
    movl %eax, (%edi)

    leal page_table_l3, %edi
    leal page_table_l2, %eax
    orl $3, %eax
    movl %eax, (%edi)

    leal page_table_l2, %edi
    xorl %ecx, %ecx
    movl $0x200, %ebx
1:
    movl %ecx, %eax
    shll $21, %eax
    orl $0x83, %eax
    movl %eax, (%edi)
    addl $8, %edi
    incl %ecx
    cmpl %ebx, %ecx
    jne 1b

    movl %cr4, %eax
    orl $0x620, %eax
    movl %eax, %cr4

    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    movl %cr0, %eax
    orl $0x80000001, %eax
    movl %eax, %cr0

    lgdt gdt64_ptr

    ljmpl $0x08, $long_mode_entry

check_cpuid:
    pushfl
    popl %eax
    movl %eax, %ecx
    xorl $0x200000, %eax
    pushl %eax
    popfl
    pushfl
    popl %eax
    pushl %ecx
    popfl
    cmpl %eax, %ecx
    je no_cpuid
    ret

no_cpuid:
    movl $msg_no_cpuid, %esi
    call serial_print
    jmp halt_32

check_long_mode:
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb no_long_mode

    movl $0x80000001, %eax
    cpuid
    testl $0x20000000, %edx
    jz no_long_mode
    ret

no_long_mode:
    movl $msg_no_long, %esi
    call serial_print
    jmp halt_32

serial_print:
    pushl %ebp
    movl %esp, %ebp
    pushl %eax
    pushl %edx
2:
    lodsb
    testb %al, %al
    jz 3f
    movw $0x3F8, %dx
    addw $5, %dx
4:
    inb %dx, %al
    testb $0x20, %al
    jz 4b
    subw $5, %dx
    outb %al, %dx
    jmp 2b
3:
    popl %edx
    popl %eax
    popl %ebp
    ret

halt_32:
    hlt
    jmp halt_32

msg_no_cpuid: .asciz "UniqOS: CPUID not supported\n"
msg_no_long:  .asciz "UniqOS: Long mode not supported\n"

.balign 8
mb_magic:
    .long 0
mb_info:
    .long 0
    .long 0

.align 16
gdt64:
    .quad 0
    .quad 0x00209A0000000000
    .quad 0x0000920000000000
gdt64_end:

gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

.align 4096
page_table_l4:
    .skip 4096
page_table_l3:
    .skip 4096
page_table_l2:
    .skip 4096

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

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

    movl mb_magic, %edi
    movslq mb_info, %rsi
    call kernel_main

halt_64:
    hlt
    jmp halt_64
