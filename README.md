# ArchaOS

**ArchaOS** is a hobby 32-bit x86 operating system written entirely from scratch in **C** and **x86 Assembly** — no libc, no Linux, no borrowed kernel code.

It boots from an ISO image into a fully custom kernel featuring a Brand new **VGA graphical desktop environment**, an already existin complete **interactive CLI shell**, a **virtual filesystem**, a super simple **memory manager**, and a small suite of built-in GUI applications.

> ⚠ArchaOS is a hobby project

---

## ✨ What's in v4.0

### Graphical Desktop (Mode 13h — 320×200, 256 colours)
- Full **window manager** — drag, minimize, maximize (fullscreen), split-screen snap, close
- **100% double-buffered** rendering — zero tearing, zero flickering, zero mouse trails
- **PS/2 mouse** support with hardware-accurate 3-byte packet parsing
- **Adaptive ProcessBar** (taskbar) with pixel-art program icons that scales with open windows
- **Start Menu** with an intricate vertical ArchaOS side banner
- Live **clock** reading directly from the CMOS RTC
- **4 themes** — Classic Teal, Win31 Navy, Matrix Green, Amber Gold
- **4 wallpapers** — Solid, Starfield, Cyber Grid, Sunset Lines
- Settings saved automatically to `archaos.conf` on the RAM disk

### Built-in GUI Applications
| Applications      |
|-------------------|
| **Calculator**    | 4-function arithmetic with GUI button grid |
| **Painter**       | Drawing tool — pencil, brush, eraser, 6-colour palette, canvas clear |
| **Minesweeper**   | 8×8 minefield with PRNG mine placement, right-click flags, win/loss detection and 3-second auto-reset |
| **Notepad**       | Full interactive text editor — types all ASCII (upper/lowercase, digits, symbols via Shift), blinking cursor, Save to RAM disk |
| **File Manager**  | Navigate the virtual filesystem, click to enter folders, file extension associations (`.txt`→Notepad, `.bmp`→Painter) |
| **Control Panel** | Theme and wallpaper selector — changes saved to `archaos.conf` |
|-------------------|

### CLI Shell (`Arc/>`)
- History (↑/↓), left/right cursor movement, Home/End, Shift+PgUp/PgDn scrollback (200-line buffer)
- Piping (`|`), output redirection (`>`, `>>`), aliases (`alias` / `unalias`)
- Script runner (`run <file>`), `grep`, `wc`

#### Built-in Commands
```
help       clear      echo       reboot     halt       uptime
date       neofetch   fortune    theme      sleep      calc
ls         pwd        cd         mkdir      touch      cat
rm         write      cp         mv         edit       ports
beep       credits    meminfo    memtest    ai         gui
```

### Virtual Filesystem
- Hierarchical in-memory RAM disk (up to 64 nodes)
- Full path resolution — absolute and relative paths
- `fs_write`, `fs_cat`, `fs_rm`, `fs_cp`, `fs_mv`, `fs_mkdir`, `fs_touch`

### Memory Manager
- 8 MB static heap with first-fit allocation
- Block splitting and coalescing
- 8-byte alignment — `kmalloc` / `kfree` / `mm_stats`

### Super Dumb AI Assistant (`ai` command)
- Hybrid fuzzy QA lookup (Levenshtein distance over ~100 pairs) + character-level GRU neural network
- Custom x87 FPU inference in bare-metal C — no ML frameworks, no libc

###  Kernel Internals
- **Bootloader**: Multiboot-compliant, GRUB 2 compatible
- **Protected Mode** via GRUB; custom IDT with 20 CPU exception handlers + PIT/keyboard/RTC/mouse IRQs
- **PIT** timer at 1000 Hz (1ms ticks) for `sleep`, GUI animation, and Minesweeper timers
- **PS/2 Keyboard**: full scancode map, Shift, Caps Lock, history navigation
- **RTC**: direct CMOS reads for live clock display
- **Boot splash**: animated ArchaOS pixel-art logo with LCG randomised border fill
- **Neofetch**: system info with ASCII logo and colour palette swatch
- **Fortune**: 15 tech quotes seeded by PIT entropy

---

## Toolchain

| Tool | Purpose |
|------|---------|
| `gcc`  | Compile freestanding C (32-bit multilib) |
| `nasm` | Assemble `.asm` sources (GNU binutils) |
| `ld`   | Link ELF32 binary |
| `grub-mkrescue` | Build bootable ISO |
| `qemu-system-i386` | Run in emulation |
| `xorriso` | Required by grub-mkrescue |

### Install on Debian/Ubuntu
```bash
sudo apt install gcc-multilib nasm binutils grub-pc-bin grub-common xorriso qemu-system-x86
```

---

## Build & Run

### Build the kernel binary
```bash
make
```

### Build a bootable ISO + run in QEMU (no sound)
```bash
make run-nosound
```

### Build a bootable ISO + run with PC speaker audio
```bash
make run
```

### Clean build artefacts
```bash
make clean
```

---

## Project Structure

```
ArchaOS/
├── src/
│   ├── boot.asm          # Multiboot entry point, stack setup, FPU init
│   ├── isr_stubs.asm     # CPU exception & IRQ assembly stubs
│   ├── kernel.c          # Kernel main, CLI shell, command dispatcher
│   ├── vga.c             # VGA text-mode driver, prompt, scrollback
│   ├── gui.c             # Mode 13h desktop, window manager, all GUI apps
│   ├── idt.c             # IDT, PIC remap, keyboard/mouse/RTC/PIT handlers
│   ├── mm.c              # Heap memory manager (kmalloc/kfree)
│   ├── fs.c              # Virtual in-memory filesystem
│   ├── editor.c          # Full-screen CLI text editor (Ctrl+S to save)
│   ├── shellext.c        # Pipes, redirection, aliases, grep, wc
│   ├── ai.c              # AI chat — fuzzy QA + GRU neural network
│   ├── pit.c             # PIT timer driver (1000 Hz)
│   ├── splash.c          # Boot splash animation
│   ├── neofetch.c        # System info display
│   ├── fortune.c         # Random tech quotes
│   ├── theme.c           # CLI colour theme switcher
│   └── linker.ld         # ELF32 linker script
├── Makefile
└── README.md
```

---

## Usage Tips

- Type `gui` in the shell to launch the graphical desktop
- Press **ESC** inside the GUI to return to the shell
- **Right-click** in Minesweeper to place / remove a flag
- Type `help` in the shell for a full command list
- Theme and wallpaper changes in Control Panel are persisted in `archaos.conf`

---

## Licence

MIT — do whatever you want with it. Credits appreciated!

---

*Built by [AkshajCreator](https://github.com/AkshajCreator)*
