CC = gcc
LD = ld
ASM = nasm

BUILD = build

CFLAGS = \
	-m32 \
	-ffreestanding \
	-O2 \
	-Wall \
	-Wextra \
	-nostdlib

LDFLAGS = \
	-T src/linker.ld \
	-nostdlib \
	-melf_i386

KERNEL_OBJS = \
	$(BUILD)/kernel.o \
	$(BUILD)/vga.o \
	$(BUILD)/boot.o

all: $(BUILD)/kernel.bin

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/kernel.o: src/kernel.c src/kernel.h src/vga.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/vga.o: src/vga.c src/vga.h src/kernel.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot.o: src/boot.asm | $(BUILD)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/kernel.bin: $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

clean:
	rm -rf $(BUILD)

run: all
	qemu-system-i386 -kernel $(BUILD)/kernel.bin

run-audio: all
	qemu-system-i386 \
		-kernel $(BUILD)/kernel.bin \
		-audiodev pa,id=snd \
		-device sb16,audiodev=snd

debug: all
	qemu-system-i386 \
		-kernel $(BUILD)/kernel.bin \
		-s -S

.PHONY: all clean run run-audio debug
