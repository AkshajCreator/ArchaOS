# Welcome to the ArchaOS Wiki

Welcome to the official documentation and technical wiki for **ArchaOS v0.5 "Monolith"**.

**ArchaOS** is an independent, 32-bit x86 monolithic operating system written entirely from scratch in **C** and **x86 Assembly**. It runs without Linux, without GRUB modules, without borrowed kernel code, and without external runtime libc dependencies.

---

## 🧭 Wiki Table of Contents

- **[[System Architecture|Architecture]]**: Monolithic kernel design, physical/virtual memory management, hardware interrupts, and drivers.
- **[[Graphical Desktop|Graphical-Desktop]]**: VGA Mode 13h (320×200 256 colors), double-buffered window manager, 6×3 desktop grid, and built-in GUI apps.
- **[[CLI Shell and Terminal|CLI-and-Shell]]**: Inline autosuggestions, interactive `Ctrl+R` history search, GNU nano 0.5.0 micro-editor, ANSI escape engine, and pipelines.
- **[[Web Browser & Networking|Web-Browser-and-Networking]]**: NetSurf HTML engine, JavaScript runtime, Speed Dial bookmarks, in-browser media player, RTL8139 networking, and BearSSL TLS.
- **[[Development & Building Guide|Development-and-Building]]**: Compiling from source, running in QEMU/VirtualBox, flashing to USB, and developing userland apps.

---

## ⚡ Core Philosophy & Design

1. **Zero Runtime Dependencies**: ArchaOS does not rely on GCC runtime libraries, musl, or glibc. Every string helper, memory allocator, format parser, and driver is custom-crafted.
2. **Zero Persistent Disk Footprint**: ArchaOS runs 100% in ephemeral system memory (RAM). It never touches, writes to, or mounts persistent user disks (ATA/IDE). Every reboot resets to a fresh, pristine environment.
3. **Dual-Mode Video Architecture**: Features an automated hardware fallback supporting both legacy VGA BIOS (Mode 13h & 80×25 text mode) and modern UEFI GOP (Graphics Output Protocol) 32-bit linear framebuffers.
4. **Authentic Vintage Aesthetics**: Mode 13h 256-color palette with custom drop shadows, active cyan glow window borders, and retro desktop icons.

---

## 🚀 Quick Start (Running ArchaOS)

### Download Pre-built ISO
Download the latest `ArchaOS.iso` from the [GitHub Releases](https://github.com/AkshajCreator/ArchaOS/releases).

### Launching in QEMU
```bash
qemu-system-i386 -cdrom ArchaOS.iso -m 128M -soundhw pcspk -net nic,model=rtl8139 -net user
```

### Essential First Commands
| Command | Action |
|---|---|
| `help` | Display category-aligned shell command reference |
| `gui` | Launch Mode 13h graphical desktop environment |
| `nano <file>` | Open the GNU nano 0.5.0 text editor |
| `new` | View release notes with interactive pagination |
| `general` | View complete system manual with word wrap |
| `neofetch` | Print system hardware summary and ASCII logo |
| `matrix` | Enter the digital rain terminal screensaver |
