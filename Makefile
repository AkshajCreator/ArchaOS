# Makefile — ArchaOS v0.5 "Monolith" (BIOS / Multiboot)

CC      = gcc
AS      = nasm
LD      = ld
AR      = ar

GCC_INCLUDES := $(shell gcc -m32 -print-file-name=include)

CFLAGS  = -m32 -ffreestanding -fno-pie -fno-stack-protector -mstackrealign \
          -nostdlib -nostdinc -Wall -Wextra -O2 \
          -Isrc -Isrc/net/include -Isrc/net \
          -Isrc/net/netsurf/libparserutils/include \
          -Isrc/net/netsurf/libwapcaplet/include \
          -Isrc/net/netsurf/libhubbub/include \
          -Isrc/net/netsurf/libcss/include \
          -Isrc/net/netsurf/libdom/include \
          -Isrc/net/js -Isrc/net/tls/inc -Isrc/net/tls/src -isystem $(GCC_INCLUDES)

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
          $(SRC_DIR)/audio.c   \
          $(SRC_DIR)/video.c   \
          $(SRC_DIR)/gui.c \
          $(SRC_DIR)/matrix.c \
          $(SRC_DIR)/less.c \
          $(SRC_DIR)/wget.c \
          $(SRC_DIR)/serial.c \
          $(SRC_DIR)/pci.c \
          $(SRC_DIR)/interpreter.c \
          $(SRC_DIR)/micropython/mp_core.c \
          $(SRC_DIR)/tcc/tcc_core.c \
          $(SRC_DIR)/ata.c \
          $(SRC_DIR)/net/e1000.c \
          $(SRC_DIR)/net/net.c \
          $(SRC_DIR)/net/arp.c \
          $(SRC_DIR)/net/ip.c \
          $(SRC_DIR)/net/dhcp.c \
          $(SRC_DIR)/net/dns.c \
          $(SRC_DIR)/net/tcp.c \
          $(SRC_DIR)/net/tls.c \
          $(SRC_DIR)/net/http.c \
          $(SRC_DIR)/net/html.c \
          $(SRC_DIR)/net/browse.c \
          $(SRC_DIR)/net/nim.c \
          $(SRC_DIR)/net/js/js_shim.c \
          $(SRC_DIR)/net/js/js_engine.c \
          $(SRC_DIR)/net/netsurf_shim.c \
          $(SRC_DIR)/net/ns_engine.c \
          $(SRC_DIR)/net/image_decoder.c \
          $(SRC_DIR)/net/stb_vorbis.c \
          $(SRC_DIR)/net/media_fetch.c

COBJS   = $(patsubst $(SRC_DIR)/%.c,  $(OBJ_DIR)/%.o, $(CSRCS))
AOBJS   = $(OBJ_DIR)/boot.o $(OBJ_DIR)/isr_stubs.o
OBJS    = $(AOBJS) $(COBJS)

BEARSSL_LIB = $(OBJ_DIR)/libbearssl.a
BEARSSL_SRCS = $(shell find $(SRC_DIR)/net/tls/src -name "*.c")
BEARSSL_OBJS = $(patsubst $(SRC_DIR)/net/tls/src/%.c, $(OBJ_DIR)/tls/%.o, $(BEARSSL_SRCS))

DUKTAPE_LIB = $(OBJ_DIR)/libduktape.a
NETSURF_LIB = $(OBJ_DIR)/libnetsurf.a

KERNEL  = $(OBJ_DIR)/kernel.bin
ISO     = ArchaOS.iso

.PHONY: all clean iso run run-serial serial

all: iso

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR) $(OBJ_DIR)/micropython $(OBJ_DIR)/tcc $(OBJ_DIR)/net $(OBJ_DIR)/tls $(OBJ_DIR)/net/js $(OBJ_DIR)/js

$(OBJ_DIR)/boot.o: $(SRC_DIR)/boot.asm | $(OBJ_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/isr_stubs.o: $(SRC_DIR)/isr_stubs.asm | $(OBJ_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/tls/%.o: $(SRC_DIR)/net/tls/src/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BEARSSL_LIB): $(BEARSSL_OBJS)
	$(AR) rcs $@ $(BEARSSL_OBJS)

$(OBJ_DIR)/js/duktape.o: $(SRC_DIR)/net/js/duktape.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(DUKTAPE_LIB): $(OBJ_DIR)/js/duktape.o
	$(AR) rcs $@ $<

$(NETSURF_LIB): | $(OBJ_DIR)
	@rm -rf /tmp/ns_objs && mkdir -p /tmp/ns_objs/pu /tmp/ns_objs/wap /tmp/ns_objs/hub /tmp/ns_objs/css /tmp/ns_objs/dom
	@cd /tmp/ns_objs/pu  && ar x /home/akushaji/ArchaOS/src/net/netsurf/libparserutils/build-x86_64-linux-gnu-x86_64-linux-gnu-release-lib-static/libparserutils.a
	@cd /tmp/ns_objs/wap && ar x /home/akushaji/ArchaOS/src/net/netsurf/libwapcaplet/build-x86_64-linux-gnu-x86_64-linux-gnu-release-lib-static/libwapcaplet.a
	@cd /tmp/ns_objs/hub && ar x /home/akushaji/ArchaOS/src/net/netsurf/libhubbub/build-x86_64-linux-gnu-x86_64-linux-gnu-release-lib-static/libhubbub.a
	@cd /tmp/ns_objs/css && ar x /home/akushaji/ArchaOS/src/net/netsurf/libcss/build-x86_64-linux-gnu-x86_64-linux-gnu-release-lib-static/libcss.a
	@cd /tmp/ns_objs/dom && ar x /home/akushaji/ArchaOS/src/net/netsurf/libdom/build-x86_64-linux-gnu-x86_64-linux-gnu-release-lib-static/libdom.a
	@ar rcs $@ /tmp/ns_objs/*/*.o

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) $(BEARSSL_LIB) $(DUKTAPE_LIB) $(NETSURF_LIB)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(NETSURF_LIB) $(BEARSSL_LIB) $(DUKTAPE_LIB)

iso: $(KERNEL)
	mkdir -p iso/boot/grub
	cp $(KERNEL)      iso/boot/kernel.bin
	cp src/grub.cfg   iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) iso
	rm -rf iso

run: iso
	qemu-system-i386 -cdrom $(ISO) -m 128 \
	  -nic user,model=e1000 \
	  -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

run-serial: iso
	qemu-system-i386 -cdrom $(ISO) -m 128 \
	  -nic user,model=e1000 \
	  -serial stdio \
	  -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0

serial: run-serial

clean:
	rm -rf $(OBJ_DIR) build_uefi $(ISO) ArchaOS-Classic.iso ArchaOS-UEFI.iso
