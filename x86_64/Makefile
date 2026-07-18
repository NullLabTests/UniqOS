CC     := clang-21
AS     := as
LD     := ld.bfd
OBJCPY := objcopy

CFLAGS := -target x86_64-none-elf -ffreestanding -nostdlib -fno-stack-protector \
          -fno-pic -fno-pie -mno-red-zone -mcmodel=large -O2 -Wall -Wextra \
          -I.

ASFLAGS := --64
LDFLAGS := -T link.ld -nostdlib -static

SRC_C := $(filter-out terminal.c display_test.c, $(wildcard *.c))
OBJ   := $(SRC_C:.c=.o) boot.o interrupts.o

.PHONY: all iso run-vbox run-qemu clean

all: uniqos.elf

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
	xorriso -as mkisofs -R -b boot/grub/i386-pc/eltorito.img -no-emul-boot -boot-load-size 4 -boot-info-table -c boot.catalog -o uniqos.iso iso/ 2>/dev/null

run-vbox: iso
	VBoxManage startvm "UniqOS" 2>/dev/null || \
	  echo "Create a VM named 'UniqOS' with uniqos.iso attached"

run-qemu: iso
	qemu-system-x86_64 -cdrom uniqos.iso -m 512M -serial stdio

clean:
	rm -f *.o uniqos.elf uniqos.iso
	rm -rf iso
