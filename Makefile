CC      = gcc
LD      = ld
CFLAGS  = -m32 -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-pic -fno-pie \
          -fno-stack-protector -mno-sse -mno-mmx -nostdlib
LDFLAGS = -m elf_i386 -T linker.ld -z noexecstack

all: minios.iso

boot.o: boot.s
	$(CC) -m32 -c boot.s -o boot.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

kernel.elf: boot.o kernel.o linker.ld
	$(LD) $(LDFLAGS) -o kernel.elf boot.o kernel.o

minios.iso: kernel.elf
	mkdir -p iso/boot/grub
	cp kernel.elf iso/boot/kernel.elf
	printf 'set timeout=0\nmenuentry "MiniOS" {\n  multiboot /boot/kernel.elf\n}\n' > iso/boot/grub/grub.cfg
	grub-mkrescue -o minios.iso iso

run: kernel.elf
	qemu-system-i386 -kernel kernel.elf

run-iso: minios.iso
	qemu-system-i386 -cdrom minios.iso

clean:
	rm -rf *.o kernel.elf iso minios.iso
