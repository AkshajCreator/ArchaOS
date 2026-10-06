# Makefile — ArchaOS v0.5 "Monolith" (BIOS / Multiboot)

CROSS_COMPILE ?=
CC      = $(CROSS_COMPILE)gcc
AS      = nasm
LD      = $(CROSS_COMPILE)ld
AR      = $(CROSS_COMPILE)ar

CFLAGS  = -m32 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -mstackrealign \
          -nostdlib -Wall -Wextra -O2 \
          -Isrc -Isrc/net/include -Isrc/net \
          -Isrc/net/netsurf/libparserutils/include \
          -Isrc/net/netsurf/libwapcaplet/include \
          -Isrc/net/netsurf/libhubbub/include \
          -Isrc/net/netsurf/libcss/include \
          -Isrc/net/netsurf/libdom/include \
          -Isrc/net/js -Isrc/net/tls/inc -Isrc/net/tls/src

ASFLAGS = -f elf32

LDFLAGS = -m elf_i386 -T src/linker.ld --oformat=elf32-i386

SRC_DIR = src
OBJ_DIR = build

CSRCS   = $(SRC_DIR)/kernel.c \
          $(SRC_DIR)/gdt.c    \
          $(SRC_DIR)/vmm.c    \
          $(SRC_DIR)/task.c   \
          $(SRC_DIR)/syscall.c \
          $(SRC_DIR)/elf.c    \
          $(SRC_DIR)/vga.c    \
          $(SRC_DIR)/idt.c    \
          $(SRC_DIR)/mm.c     \
          $(SRC_DIR)/fs.c     \
          $(SRC_DIR)/string.c \
          $(SRC_DIR)/keyboard.c \
          $(SRC_DIR)/editor.c \
          $(SRC_DIR)/pit.c    \
          $(SRC_DIR)/splash.c \
          $(SRC_DIR)/vesa.c   \
          $(SRC_DIR)/font_engine.c \
          $(SRC_DIR)/neofetch.c \
          $(SRC_DIR)/fortune.c  \
          $(SRC_DIR)/theme.c    \
          $(SRC_DIR)/shellext.c \
          $(SRC_DIR)/audio.c   \
          $(SRC_DIR)/video.c   \
          $(SRC_DIR)/gui.c \
          $(SRC_DIR)/matrix.c \
          $(SRC_DIR)/coreview.c \
          $(SRC_DIR)/less.c \
          $(SRC_DIR)/wget.c \
          $(SRC_DIR)/tar.c \
          $(SRC_DIR)/gamefetch.c \
          $(SRC_DIR)/mdview.c \
          $(SRC_DIR)/hexedit.c \
          $(SRC_DIR)/archmux.c \
          $(SRC_DIR)/iso9660.c \
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
          $(SRC_DIR)/net/js/js_shim.c \
          $(SRC_DIR)/net/js/js_engine.c \
          $(SRC_DIR)/net/netsurf_shim.c \
          $(SRC_DIR)/net/ns_engine.c \
          $(SRC_DIR)/net/image_decoder.c \
          $(SRC_DIR)/net/stb_vorbis.c \
          $(SRC_DIR)/net/media_fetch.c

COBJS   = $(patsubst $(SRC_DIR)/%.c,  $(OBJ_DIR)/%.o, $(CSRCS))
AOBJS   = $(OBJ_DIR)/boot.o $(OBJ_DIR)/isr_stubs.o $(OBJ_DIR)/user_assets.o
OBJS    = $(AOBJS) $(COBJS)

BEARSSL_LIB = $(OBJ_DIR)/libbearssl.a
BEARSSL_SRCS = $(shell find $(SRC_DIR)/net/tls/src -name "*.c")
BEARSSL_OBJS = $(patsubst $(SRC_DIR)/net/tls/src/%.c, $(OBJ_DIR)/tls/%.o, $(BEARSSL_SRCS))

DUKTAPE_LIB = $(OBJ_DIR)/libduktape.a
NETSURF_LIB = $(OBJ_DIR)/libnetsurf.a

USER_DIR = user
USER_INC = $(USER_DIR)/include
USER_LIB = $(USER_DIR)/lib
USER_BIN = $(USER_DIR)/bin

USER_CRT0 = $(OBJ_DIR)/user/crt0.o
USER_LIBC = $(OBJ_DIR)/user/libc.o
USER_GUI  = $(OBJ_DIR)/user/gui.o
USER_SRCS  = $(wildcard $(USER_BIN)/*.c)
USER_PROGS = $(patsubst $(USER_BIN)/%.c, %, $(USER_SRCS))
USER_ELFS  = $(patsubst %, $(OBJ_DIR)/bin/%.elf, $(USER_PROGS))

LIBGCC ?= $(shell $(CC) -m32 -print-libgcc-file-name)

DOOM_DIR = $(USER_BIN)/doomgeneric
DOOM_CSRCS = $(DOOM_DIR)/dummy.c $(DOOM_DIR)/am_map.c $(DOOM_DIR)/doomdef.c $(DOOM_DIR)/doomstat.c \
             $(DOOM_DIR)/dstrings.c $(DOOM_DIR)/d_event.c $(DOOM_DIR)/d_items.c $(DOOM_DIR)/d_iwad.c \
             $(DOOM_DIR)/d_loop.c $(DOOM_DIR)/d_main.c $(DOOM_DIR)/d_mode.c $(DOOM_DIR)/d_net.c \
             $(DOOM_DIR)/f_finale.c $(DOOM_DIR)/f_wipe.c $(DOOM_DIR)/g_game.c $(DOOM_DIR)/hu_lib.c \
             $(DOOM_DIR)/hu_stuff.c $(DOOM_DIR)/info.c $(DOOM_DIR)/i_cdmus.c $(DOOM_DIR)/i_endoom.c \
             $(DOOM_DIR)/i_joystick.c $(DOOM_DIR)/i_scale.c $(DOOM_DIR)/i_sound.c $(DOOM_DIR)/i_system.c \
             $(DOOM_DIR)/i_timer.c $(DOOM_DIR)/memio.c $(DOOM_DIR)/m_argv.c $(DOOM_DIR)/m_bbox.c \
             $(DOOM_DIR)/m_cheat.c $(DOOM_DIR)/m_config.c $(DOOM_DIR)/m_controls.c $(DOOM_DIR)/m_fixed.c \
             $(DOOM_DIR)/m_menu.c $(DOOM_DIR)/m_misc.c $(DOOM_DIR)/m_random.c $(DOOM_DIR)/p_ceilng.c \
             $(DOOM_DIR)/p_doors.c $(DOOM_DIR)/p_enemy.c $(DOOM_DIR)/p_floor.c $(DOOM_DIR)/p_inter.c \
             $(DOOM_DIR)/p_lights.c $(DOOM_DIR)/p_map.c $(DOOM_DIR)/p_maputl.c $(DOOM_DIR)/p_mobj.c \
             $(DOOM_DIR)/p_plats.c $(DOOM_DIR)/p_pspr.c $(DOOM_DIR)/p_saveg.c $(DOOM_DIR)/p_setup.c \
             $(DOOM_DIR)/p_sight.c $(DOOM_DIR)/p_spec.c $(DOOM_DIR)/p_switch.c $(DOOM_DIR)/p_telept.c \
             $(DOOM_DIR)/p_tick.c $(DOOM_DIR)/p_user.c $(DOOM_DIR)/r_bsp.c $(DOOM_DIR)/r_data.c \
             $(DOOM_DIR)/r_draw.c $(DOOM_DIR)/r_main.c $(DOOM_DIR)/r_plane.c $(DOOM_DIR)/r_segs.c \
             $(DOOM_DIR)/r_sky.c $(DOOM_DIR)/r_things.c $(DOOM_DIR)/sha1.c $(DOOM_DIR)/sounds.c \
             $(DOOM_DIR)/statdump.c $(DOOM_DIR)/st_lib.c $(DOOM_DIR)/st_stuff.c $(DOOM_DIR)/s_sound.c \
             $(DOOM_DIR)/tables.c $(DOOM_DIR)/v_video.c $(DOOM_DIR)/wi_stuff.c $(DOOM_DIR)/w_checksum.c \
             $(DOOM_DIR)/w_file.c $(DOOM_DIR)/w_main.c $(DOOM_DIR)/w_wad.c $(DOOM_DIR)/z_zone.c \
             $(DOOM_DIR)/w_file_stdc.c $(DOOM_DIR)/i_input.c $(DOOM_DIR)/i_video.c $(DOOM_DIR)/doomgeneric.c \
             $(DOOM_DIR)/doomgeneric_archaos.c
DOOM_OBJS = $(patsubst $(DOOM_DIR)/%.c, $(OBJ_DIR)/doom/%.o, $(DOOM_CSRCS))

SCUMM_DIR = $(USER_BIN)/scummvm
SCUMM_SRCS = $(shell find $(SCUMM_DIR) -name "*.cpp" -not -path "*/games/*") $(USER_LIB)/cxx_runtime.cpp
SCUMM_OBJS = $(patsubst $(USER_DIR)/%, $(OBJ_DIR)/scummvm_build/%, $(patsubst %.cpp, %.o, $(SCUMM_SRCS)))

USER_ELFS += $(OBJ_DIR)/bin/doomgeneric.elf $(OBJ_DIR)/bin/scummvm.elf

KERNEL  = $(OBJ_DIR)/kernel.bin
ISO     = ArchaOS.iso

.PHONY: all clean iso run run-serial serial user

all: iso

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR) $(OBJ_DIR)/micropython $(OBJ_DIR)/tcc $(OBJ_DIR)/net $(OBJ_DIR)/tls $(OBJ_DIR)/net/js $(OBJ_DIR)/js $(OBJ_DIR)/user $(OBJ_DIR)/bin $(OBJ_DIR)/doom $(OBJ_DIR)/scummvm_build

$(USER_CRT0): $(USER_DIR)/crt0.asm | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/user
	$(AS) $(ASFLAGS) $< -o $@

$(USER_LIBC): $(USER_LIB)/libc.c $(USER_LIB)/libc.h $(USER_LIB)/syscall.h | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/user
	$(CC) -I$(USER_INC) -I$(USER_LIB) $(CFLAGS) -c $< -o $@

$(USER_GUI): $(USER_LIB)/gui.c $(USER_INC)/gui.h $(USER_LIB)/libc.h $(USER_LIB)/syscall.h | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/user
	$(CC) -I$(USER_INC) -I$(USER_LIB) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/user/%.o: $(USER_BIN)/%.c $(USER_INC)/gui.h $(USER_INC)/archaos.h | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/user
	$(CC) -I$(USER_INC) -I$(USER_LIB) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/doom/%.o: $(DOOM_DIR)/%.c $(DOOM_DIR)/doomgeneric.h | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/doom
	$(CC) -I$(USER_INC) -I$(USER_LIB) -I$(DOOM_DIR) -DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -DFILES_DIR='"/DOOM"' $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/bin/doomgeneric.elf: $(USER_CRT0) $(USER_LIBC) $(USER_GUI) $(DOOM_OBJS) $(USER_DIR)/linker.ld | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/bin
	$(LD) -m elf_i386 -T $(USER_DIR)/linker.ld --oformat=elf32-i386 -o $@ $(USER_CRT0) $(USER_LIBC) $(USER_GUI) $(DOOM_OBJS) $(LIBGCC)

$(OBJ_DIR)/bin/doom.elf: $(OBJ_DIR)/bin/doomgeneric.elf
	cp $< $@

$(OBJ_DIR)/scummvm_build/%.o: $(USER_DIR)/%.cpp | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	g++ -m32 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -mstackrealign -nostdlib -fno-exceptions -fno-rtti -fpermissive -O2 -DHAVE_CONFIG_H -DUNIX -DARCHAOS -I$(SCUMM_DIR) -I$(SCUMM_DIR)/common -I$(USER_INC) -I$(USER_LIB) -c $< -o $@

$(OBJ_DIR)/bin/scummvm.elf: $(USER_CRT0) $(USER_LIBC) $(USER_GUI) $(SCUMM_OBJS) $(USER_DIR)/linker.ld | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/bin
	$(LD) -m elf_i386 -T $(USER_DIR)/linker.ld --oformat=elf32-i386 -o $@ $(USER_CRT0) $(USER_LIBC) $(USER_GUI) $(SCUMM_OBJS) $(LIBGCC)

$(OBJ_DIR)/bin/%.elf: $(USER_CRT0) $(USER_LIBC) $(USER_GUI) $(OBJ_DIR)/user/%.o $(USER_DIR)/linker.ld | $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/bin
	$(LD) -m elf_i386 -T $(USER_DIR)/linker.ld --oformat=elf32-i386 -o $@ $(USER_CRT0) $(USER_LIBC) $(USER_GUI) $(OBJ_DIR)/user/$*.o $(LIBGCC)

.SECONDARY: $(USER_CRT0) $(USER_LIBC) $(USER_GUI) $(patsubst %, $(OBJ_DIR)/user/%.o, $(USER_PROGS)) $(DOOM_OBJS) $(SCUMM_OBJS)

user: $(USER_ELFS)

$(SRC_DIR)/user_binaries.h: $(USER_ELFS) tools/embed_user_binaries.py
	python3 tools/embed_user_binaries.py $@ $(USER_ELFS)

$(SRC_DIR)/user_assets.h $(SRC_DIR)/user_assets.asm: $(wildcard user/bin/doomgeneric/DOOM/*) $(shell find user/bin/scumm/games -type f 2>/dev/null) tools/embed_user_assets.py
	python3 tools/embed_user_assets.py $(SRC_DIR)/user_assets.h user/bin/doomgeneric/DOOM user/bin/scumm/games

$(OBJ_DIR)/user_assets.o: $(SRC_DIR)/user_assets.asm | $(OBJ_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/elf.o: $(SRC_DIR)/user_binaries.h $(SRC_DIR)/user_assets.h

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

HOSTCC  ?= gcc
LIMINE_DIR = boot/limine
LIMINE_TOOL = $(OBJ_DIR)/limine

$(LIMINE_TOOL): $(LIMINE_DIR)/limine.c | $(OBJ_DIR)
	$(HOSTCC) -O2 -pipe -Wall -Wextra -std=c99 $< -o $@

# Limine ISO (Dual Legacy BIOS + UEFI Bootable)
limine-iso: $(KERNEL) $(LIMINE_TOOL)
	rm -rf iso
	mkdir -p iso/boot iso/EFI/BOOT
	cp $(KERNEL) iso/boot/kernel.bin
	cp $(LIMINE_DIR)/limine.conf iso/boot/limine.conf
	cp $(LIMINE_DIR)/limine.conf iso/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys iso/
	cp $(LIMINE_DIR)/limine-bios-cd.bin iso/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin iso/
	cp $(LIMINE_DIR)/BOOTIA32.EFI iso/EFI/BOOT/
	cp $(LIMINE_DIR)/BOOTX64.EFI iso/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso -o $(ISO)
	$(LIMINE_TOOL) bios-install $(ISO)
	rm -rf iso

# Legacy GRUB ISO
grub-iso: $(KERNEL)
	rm -rf iso
	mkdir -p iso/boot/grub
	cp $(KERNEL)      iso/boot/kernel.bin
	cp src/grub.cfg   iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) iso
	rm -rf iso

# Default ISO target: Limine Bootloader
iso: limine-iso

run: iso
	qemu-system-i386 -cdrom $(ISO) -m 128 \
	  -nic user,model=e1000 \
	  -audiodev pa,id=snd0 -device sb16,audiodev=snd0 -machine pcspk-audiodev=snd0

run-limine: limine-iso
	qemu-system-i386 -cdrom $(ISO) -m 128 \
	  -nic user,model=e1000 \
	  -audiodev pa,id=snd0 -device sb16,audiodev=snd0 -machine pcspk-audiodev=snd0

run-grub: grub-iso
	qemu-system-i386 -cdrom $(ISO) -m 128 \
	  -nic user,model=e1000 \
	  -audiodev pa,id=snd0 -device sb16,audiodev=snd0 -machine pcspk-audiodev=snd0

run-serial: iso
	qemu-system-i386 -cdrom $(ISO) -m 128 \
	  -nic user,model=e1000 \
	  -serial stdio \
	  -audiodev pa,id=snd0 -device sb16,audiodev=snd0 -machine pcspk-audiodev=snd0

serial: run-serial

clean:
	rm -rf $(OBJ_DIR) build_uefi $(ISO) ArchaOS-Classic.iso ArchaOS-UEFI.iso $(SRC_DIR)/user_binaries.h iso
