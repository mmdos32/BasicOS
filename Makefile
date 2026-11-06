CC      = gcc
LD      = ld
CFLAGS  = -m32 -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-pic -fno-pie \
          -fno-stack-protector -fno-tree-loop-distribute-patterns \
          -fno-asynchronous-unwind-tables -fcf-protection=none \
          -mno-sse -mno-mmx -nostdlib -Isrc
LDFLAGS = -m elf_i386 -T boot/linker.ld -z noexecstack

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)
HDRS = $(wildcard src/*.h)

.PHONY: all run run-serial run-iso clean

QEMU_FLAGS ?= -display gtk

all: basicos.iso

boot/boot.o: boot/boot.s
	$(CC) -m32 -c boot/boot.s -o boot/boot.o

src/%.o: src/%.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

kernel.elf: boot/boot.o $(OBJS) boot/linker.ld
	$(LD) $(LDFLAGS) -o kernel.elf boot/boot.o $(OBJS)

basicos.iso: kernel.elf
	mkdir -p iso/boot/grub
	cp kernel.elf iso/boot/kernel.elf
	printf 'set timeout=0\nmenuentry "BasicOS" {\n  multiboot /boot/kernel.elf\n}\n' > iso/boot/grub/grub.cfg
	grub-mkrescue -o basicos.iso iso

run: kernel.elf
	qemu-system-i386 $(QEMU_FLAGS) -kernel kernel.elf

# also mirrors all output to this terminal through the serial port
run-serial: kernel.elf
	qemu-system-i386 $(QEMU_FLAGS) -serial stdio -kernel kernel.elf

run-iso: basicos.iso
	qemu-system-i386 $(QEMU_FLAGS) -cdrom basicos.iso

clean:
	rm -rf boot/*.o src/*.o kernel.elf iso basicos.iso
