# ArchaOS

ArchaOS is a 32-bit x86 hobby operating system written from scratch in C and x86 assembly. It boots via Multiboot (GRUB) or directly in QEMU, establishes protected mode, and implements core operating system primitives without external C runtime dependencies.

The project began as an educational monolithic kernel and is currently being refactored to separate the core kernel and device drivers from userland applications.

---

## Features

### Kernel Core
- **Boot**: Multiboot-compliant bootloader entry (`boot.asm`) configuring protected mode and FPU.
- **CPU & Memory**: Global Descriptor Table (GDT), Interrupt Descriptor Table (IDT), PIC 8259 remapping, and two-level paging with Virtual Memory Manager (VMM).
- **Scheduler**: Preemptive round-robin task scheduler triggered by the PIT timer at 1000 Hz.
- **Syscalls & Binaries**: Software interrupt `int 0x80` syscall dispatch and ELF32 binary loader (`elf.c`).
- **Filesystem**: In-memory virtual filesystem (RAM disk) supporting hierarchical directories, file creation, reading, and writing.

### Drivers & Hardware
- **Input**: PS/2 keyboard driver with US QWERTY layout and modifier tracking, PS/2 mouse packet parsing.
- **Video**: Standard VGA 80x25 text mode, Mode 13h (320x200 256-color), and VESA BIOS Extensions (VBE) high-resolution 800x600 32-bit linear framebuffer.
- **Storage & Bus**: IDE/ATA hard disk driver and PCI bus enumeration.
- **Networking**: Intel 82540EM (E1000) and Realtek RTL8139 drivers with ARP, IPv4, UDP, TCP, DHCP, and DNS support.
- **Audio & Timing**: Sound Blaster 16 & OPL3 FM synthesizer, PC speaker frequency synth, and CMOS Real-Time Clock (RTC).
- **Serial**: 16550 UART serial driver for kernel logging and debugging via COM1.

### User Interface & Shell
- **Command Shell**: Unix-like command interpreter supporting pipelines (`|`), command chaining (`;`), environment variables, history, and tab completion.
- **Terminal Utilities**: `nano` text editor, `less` pager, `hexdump`, `neofetch`, `matrix`, `mdview` (markdown reader), `hexedit` (interactive hex editor), `archmux` (terminal multiplexer), MicroPython REPL (`python`), and standard file commands (`ls`, `cat`, `mkdir`, `cp`, `rm`, `grep`, `wc`).
- **Hardware Telemetry (`coreview`)**: Real-time silicon monitor showing CPU registers, bitfield flip transitions, memory waterfall heat map, and GPU/CRTC raster oscilloscope.
- **Graphical Desktop (`gui`)**: Mode 13h and VESA 800x600 true-color window manager featuring dual virtual workspaces (`Ctrl+1`/`Ctrl+2`), QuickRunner spotlight launcher (`Alt+Space`), 3D screensavers (Cube, Mystify, Starfield), window dragging, maximize/snapping, taskbar pager, start menu, desktop icons, and bundled applets (Notepad, File Manager, Calculator, Painter with 9 tools, Minesweeper, Snake, DOOM).

---

## Getting Started

### Prerequisites

On Debian, Ubuntu, or Pop!_OS:
```bash
sudo apt update
sudo apt install -y gcc-multilib nasm binutils grub-pc-bin grub-common xorriso qemu-system-x86
```

On Fedora:
```bash
sudo dnf install -y gcc nasm binutils grub2-tools-extra xorriso qemu-system-x86 glibc-devel.i686
```

On Arch Linux:
```bash
sudo pacman -S gcc nasm binutils grub xorriso qemu-system-x86 lib32-glibc
```

### Building the ISO

ArchaOS supports both the modern **Limine Bootloader** (default, dual BIOS + UEFI bootable) and legacy GRUB:

- **Build Limine ISO (Default)**:
  ```bash
  make iso
  ```
  *(Or explicitly `make limine-iso`)*. This compiles the self-contained Limine installer and builds a dual BIOS + UEFI bootable `ArchaOS.iso`.

- **Build Legacy GRUB ISO**:
  ```bash
  make grub-iso
  ```

### Running in QEMU

To run the compiled ISO in QEMU:
```bash
make run
```
*(Or specifically `make run-limine` or `make run-grub`)*

To run with serial debug output directed to your terminal:
```bash
make run-serial
```

---

## Prebuilt Releases

Bootable ISO images and release archives are published under [GitHub Releases](https://github.com/AkshajCreator/ArchaOS/releases). Binary archives are kept out of the git repository to keep clone sizes minimal.

---

## Repository Structure

```
ArchaOS/
├── boot/             # GRUB multiboot configuration
├── docs/             # Architecture and design documentation
├── tools/            # User binary embedding and packaging tools
├── user/             # Standalone Ring 3 User Space & Libc
│   ├── linker.ld     # Userland ELF linker script (0x08048000)
│   ├── crt0.asm      # C runtime entry point (_start)
│   ├── lib/          # Freestanding C library (libc.c/.h, syscall.h)
│   └── bin/          # Standalone user programs (hello, echo, cat, sh)
├── src/              # Monolithic Operating System Kernel (0xC0000000)
│   ├── boot.asm      # Higher-Half multiboot entry point & bootstrap paging
│   ├── isr_stubs.asm # Interrupt service routine assembly stubs
│   ├── gdt.c/.h      # Global Descriptor Table & TSS
│   ├── idt.c/.h      # Interrupt Descriptor Table & PIC
│   ├── vmm.c/.h      # Higher-Half Two-Tier Paging & VMM
│   ├── mm.c/.h       # Dynamic on-demand paged heap allocator (0xC2000000)
│   ├── task.c/.h     # Preemptive Priority Scheduler & Ring 3 context switching
│   ├── syscall.c/.h  # POSIX syscall handler (int 0x80)
│   ├── elf.c/.h      # ELF32 executable loader
│   ├── keyboard.c/.h # Centralized PS/2 keyboard driver
│   ├── string.c/.h   # Centralized C string and memory routines
│   ├── vga.c/.h      # VGA text mode console driver
│   ├── vesa.c/.h     # VESA linear framebuffer graphics
│   ├── gui.c/.h      # Desktop compositor and window manager
│   ├── coreview.c/.h # Hardware silicon telemetry monitor
│   ├── fs.c/.h       # Virtual filesystem
│   ├── editor.c/.h   # Text editor
│   ├── shellext.c/.h # Shell pipeline and environment handling
│   ├── pit.c/.h      # Programmable Interval Timer
│   ├── ata.c/.h      # ATA hard disk driver
│   ├── pci.c/.h      # PCI bus scanner
│   ├── serial.c/.h   # 16550 UART serial driver
│   ├── audio.c/.h    # PC speaker sound driver
│   ├── net/          # Network stack and device drivers
│   └── kernel.c      # Kernel entry and command dispatcher
├── tests/            # Test scripts and harnesses
└── Makefile          # Build configuration
```

---

## Roadmap

- [x] **Monolithic Higher-Half Architecture**: Kernel linked and mapped at `0xC0000000` with dual-mapped bootstrap paging.
- [x] **Userland Separation & Ring 3 Execution**: Standalone ELF binaries running in Ring 3 unprivileged mode isolated from Ring 0.
- [x] **Hardware-Level VMM Isolation**: User processes run on dedicated page directories (`user_pd`) preventing access to supervisor memory.
- [x] **Freestanding C Library**: Freestanding libc with `printf`, `malloc`, `free`, `sbrk`, `read`, `write`, `open`, `close`, `sleep`, `yield`, `spawn`, `getpid`, `exit`.
- [x] **POSIX Syscall Dispatch**: Software interrupt `int 0x80` trap dispatching between Ring 3 and Ring 0.
- [ ] **Storage Persistence**: Mount FAT12/FAT16/ext2 filesystems over the ATA driver for permanent storage.

---

## License

This project is licensed under the MIT License. See the LICENSE file for details.
