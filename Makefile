# Makefile — ArchaOS v4.0 "Phosphor"

CC      = gcc
AS      = nasm
LD      = ld

GCC_INCLUDES := $(shell gcc -m32 -print-file-name=include)

CFLAGS  = -m32 -ffreestanding -fno-pie -fno-stack-protector \
          -nostdlib -nostdinc -Wall -Wextra -O2 \
          -Isrc -isystem $(GCC_INCLUDES)

ASFLAGS = -f elf32

LDFLAGS = -m elf_i386 -T src/linker.ld --oformat=elf32-i386

SRC_DIR = src
OBJ_DIR = build

CSRCS   = $(SRC_DIR)/kernel.c \
          $(SRC_DIR)/vga.c    \
          $(SRC_DIR)/idt.c    \
          $(SRC_DIR)/mm.c     \
          $(SRC_DIR)/fs.c     \
          $(SRC_DIR)/editor.c \
          $(SRC_DIR)/ai.c     \
          $(SRC_DIR)/pit.c    \
          $(SRC_DIR)/splash.c \
          $(SRC_DIR)/neofetch.c \
          $(SRC_DIR)/fortune.c  \
          $(SRC_DIR)/theme.c    \
          $(SRC_DIR)/shellext.c \
          $(SRC_DIR)/gui.c

ASRCS   = $(SRC_DIR)/boot.asm \
          $(SRC_DIR)/isr_stubs.asm

COBJS   = $(patsubst $(SRC_DIR)/%.c,  $(OBJ_DIR)/%.o, $(CSRCS))
AOBJS   = $(patsubst $(SRC_DIR)/%.asm,$(OBJ_DIR)/%.o, $(ASRCS))

OBJS    = $(AOBJS) $(COBJS)

KERNEL  = $(OBJ_DIR)/kernel.bin
ISO     = ArchaOS.iso

.PHONY: all clean iso run

all: $(KERNEL)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.asm | $(OBJ_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

iso: $(KERNEL)
	mkdir -p iso/boot/grub
	cp $(KERNEL)      iso/boot/kernel.bin
	cp src/grub.cfg   iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) iso
	rm -rf iso

run: iso
	qemu-system-i386 -cdrom $(ISO) -m 13000 \
	  -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

run-nosound: iso
	qemu-system-i386 -cdrom $(ISO) -m 64 -no-reboot

clean:
	rm -rf $(OBJ_DIR) $(ISO)
