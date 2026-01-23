# ArchaOS

ArchaOS is a hobby x86 operating system written from scratch using **C** and **x86 Assembly**.  
It is built as a learning project to understand low-level OS concepts such as booting, VGA text mode, keyboard input, and basic command handling.

> ⚠️ ArchaOS is **not** a production OS. It is purely educational.

---

## ✨ Features (v0.2)

- Custom x86 bootloader
- Freestanding kernel (no libc)
- VGA text-mode driver
- Keyboard input handling
- Command-line shell
- Built-in commands:
  - `help`
  - `clear`
  - `echo`
  - `halt`
  - `reboot`
  - `about`
  - `beep` (PC speaker)
- Screen scrolling
- Basic prompt system (`Arc/>`)

---

## 🛠️ Toolchain

You need:

- GCC (with 32-bit multilib support)
- NASM
- GNU ld
- QEMU or Bochs

Tested on Linux.

---

## 🚀 Build Instructions

```bash
make clean
make

