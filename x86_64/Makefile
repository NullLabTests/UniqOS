CC     := clang-21
AS     := as
LD     := ld.bfd
OBJCPY := objcopy

CFLAGS   := -target x86_64-none-elf -ffreestanding -nostdlib -fno-stack-protector \
            -fno-pic -fno-pie -mno-red-zone -mcmodel=large -O2 -Wall -Wextra -I.
CFLAGS_DBG := -target x86_64-none-elf -ffreestanding -nostdlib -fno-stack-protector \
              -fno-pic -fno-pie -mno-red-zone -mcmodel=large -Og -g -Wall -Wextra -I.

ASFLAGS := --64
LDFLAGS := -T link.ld -nostdlib -static

OBJ_KERNEL := boot.o interrupts.o kernel.o gdt.o idt.o pic.o pit.o pmm.o vmm.o \
              heap.o scheduler.o syscall.o support.o serial.o
OBJ_DRIVERS := display.o keyboard.o mouse.o pci.o vga.o virtio_net.o
OBJ_NET := net.o arp.o ip.o tcp.o http.o dns.o icmp.o
OBJ_VFS := vfs.o
OBJ_APPS := desktop.o window.o fbterm.o font.o timefmt.o shell.o browser.o \
            editor.o fileman.o sysmon.o

OBJ := $(OBJ_KERNEL) $(OBJ_DRIVERS) $(OBJ_NET) $(OBJ_VFS) $(OBJ_APPS)

.PHONY: all debug iso clean test

all: CFLAGS := $(CFLAGS)
all: uniqos.elf

debug: CFLAGS := $(CFLAGS_DBG)
debug: uniqos.elf

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) -o $@ $<

uniqos.elf: $(OBJ) link.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)
	file $@

iso: uniqos.elf
	mkdir -p iso/boot/grub
	cp uniqos.elf iso/boot/
	cp grub.cfg iso/boot/grub/
	cp -r /usr/lib/grub/i386-pc iso/boot/grub/
	xorriso -as mkisofs -R -b boot/grub/i386-pc/eltorito.img -no-emul-boot \
	  -boot-load-size 4 -boot-info-table -c boot.catalog -o uniqos.iso iso/ 2>/dev/null

run-vbox: iso
	VBoxManage startvm "UniqOS" 2>/dev/null || \
	  echo "Create a VM named 'UniqOS' with uniqos.iso attached"

run-qemu: iso
	qemu-system-x86_64 -cdrom uniqos.iso -m 512M -serial stdio

clean:
	rm -f *.o uniqos.elf uniqos.iso
	rm -rf iso

test: CFLAGS := $(CFLAGS_DBG)
test: uniqos.elf
	@echo "=== Build OK ==="
	@clang-tidy --version 2>/dev/null && clang-tidy -checks='*' *.c -- $(CFLAGS) 2>/dev/null | tail -5 || echo "(clang-tidy not available)"
