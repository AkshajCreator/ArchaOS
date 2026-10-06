# ArchaOS v0.6 "Nexus" — Official Roadmap & Specification

**ArchaOS v0.6** is the next major generational evolution of ArchaOS, advancing from the v0.5 "Monolith" release to a fully preemptive, memory-protected, high-resolution, multimedia-rich operating system running 100% in RAM with zero persistent disk footprint.

---

## 🎯 Release Vision & Goals

1. **Robust Kernel Architecture**: Preemptive multitasking, x86 paging MMU, Ring 3 user mode isolation, and hardware Triple Fault prevention.
2. **True Visual & Audio Immersion**: High-resolution VESA/GOP true-color compositor, Sound Blaster 16 / AC97 PCM stereo audio, custom typography, and the signature **CoreView** hardware monitor.
3. **Rock-Solid Internet & Subsystems**: Complete rewrite of Web Browser HTML/CSS/JS rendering, non-blocking asynchronous TCP/HTTP downloads, and embedded web server.
4. **Professional Developer & Creative Tools**: Real C and MicroPython language engines in App Studio 2.0, interactive HexEdit, Markdown reader, and DOOM.
5. **System Stability & UX**: Application Not Responding ("Wait or Force Quit") watchdog, dual workspaces, Spotlight QuickRunner, and cinematic boot screen.

---

## 🌟 The 25 Marquee Features

### I. The Signature Hardware Monitor
- [x] **1. CoreView (Silicon Monitor)**: Real-time window into the machine's physical calculations.
  - Live CPU registers (`EAX`, `EBX`, `ECX`, `EDX`, `ESP`, `EBP`, `EIP`, `EFLAGS`, `CR0`, `CR3`) in dual **Hexadecimal (`0x...`)** and **32-bit Binary (`0101...`)**.
  - Dynamic **bit-flip pulse animation**: bits flash bright cyan/amber on transition.
  - Live CPU opcode peek and mnemonic disassembly at `CS:EIP`.
  - RAM memory waterfall scanner displaying active heap/stack bytes with heat glow on write.
  - GPU / VGA engine inspector: CRT Controller registers, DAC palette, and live raster beam scanline tracker (`0x3DA`).
  - Available as both a desktop GUI application and full-screen ANSI terminal command (`coreview`).

### II. Core Kernel & Execution Architecture
- [x] **2. Preemptive Multitasking & Scheduler**:
  - PIT Channel 0 / APIC timer interrupt-driven preemptive context switcher (`pushad`, stack frame swap, `iretd`).
  - Task Control Block (`struct task` / PCB) with PID, execution priority, and lifecycle states (`READY`, `RUNNING`, `SLEEPING`, `BLOCKED`, `TERMINATED`).
  - Priority round-robin scheduling with cooperative sleep and yield hooks (`sys_sleep`, `sys_yield`).
- [x] **3. Two-Tier x86 Paging MMU**:
  - Page Directory & Page Tables with 4 KB page frame allocation (`CR3`, `CR0.PG`).
  - Identity-mapped kernel memory + isolated virtual user memory address spaces.
  - Kernel memory heap dynamic page expansion.
- [x] **4. Ring 3 User Mode & Syscall Subsystem**:
  - User Code (`0x2B`) and User Data (`0x33`) segment descriptors in GDT + Task State Segment (TSS).
  - POSIX-compatible `int 0x80` system call dispatcher (`sys_read`, `sys_write`, `sys_open`, `sys_close`, `sys_spawn`, `sys_kill`, `sys_exit`, `sys_yield`).
- [x] **5. Standalone ELF32 Executable Loader**:
  - Load, relocate, and execute standalone 32-bit ELF userland binaries compiled with GCC or TCC directly from RAM disk.

### III. Display, Compositing & Desktop UX
- [x] **6. High-Res VESA VBE (BIOS) / Unified Framebuffer**:
  - VESA BIOS Extensions (VBE 2.0/3.0) supporting **640×480** and **800×600** in 24/32-bit true color.
  - Unified rendering compositor bridging BIOS VESA and UEFI GOP with 32-bit ARGB color and alpha blending.
- [x] **7. Free-Form Drag & Drop Desktop Icons**:
  - Drag and drop desktop file and app icons anywhere on the wallpaper canvas.
  - Right-click desktop menu option: *Align to Grid* and *Auto-Sort by Name / Type*.
- [x] **8. Universal Right-Click Context Menus**:
  - **Desktop Wallpaper**: *New File*, *New Directory*, *Change Wallpaper*, *Open Terminal Here*, *Arrange Icons*, *System Properties*.
  - **File Manager**: *Open*, *Open With...*, *Edit in Nano*, *Play Audio*, *Delete*, *Properties*.
- [x] **9. Dual Virtual Desktops (Workspaces 1 & 2)**:
  - Toggle between Workspace 1 and Workspace 2 instantly with `Ctrl+1` / `Ctrl+2` or `Ctrl+Tab`.
  - ProcessBar (taskbar) dual-workspace mini-pager widget (`[ 1 ] [ 2 ]`) reflecting active windows per desktop.
- [x] **10. QuickRunner / Spotlight (`Alt + Space`)**:
  - Press `Alt + Space` anywhere to summon a sleek, centered search launcher.
  - Fuzzy search for applications, tools, and RAM disk files.
  - Instant inline calculator (`512 * 1024` -> `524288`) and hex/decimal/binary converter.

### IV. Audio & Multimedia Subsystem
- [x] **11. Sound Blaster 16 & AC97 PCI Audio Driver**:
  - DMA double-buffered 16-bit 44.1 kHz stereo PCM audio playback (leaving the single-voice PC speaker behind).
  - Background audio engine feeding PCM buffers smoothly under preemptive multitasking.
- [x] **12. Desktop Event Sound Schemes**:
  - Authentic retro sound effects on system events: window open, minimize, close, button click, error buzz, and startup/shutdown chimes.

### V. System Integration, Storage & Diagnostics
- [x] **13. Global System Clipboard**:
  - Unified clipboard buffer shared across CLI nano, the shell, GUI Notepad, the Web Browser, and App Studio.
- [x] **14. Multiboot RAM Initrd (TarFS)**:
  - Load external asset archives (`initrd.tar`) via GRUB Multiboot at boot into the RAM VFS.
  - Stores wallpapers, sample code, web assets, and chiptunes without inflating kernel binary size.
- [x] **15. Application Hang Watchdog ("Wait or Force Quit")**:
  - Detects when an application event loop stops responding for > 3–5 seconds.
  - Displays a modal prompt: `"[Process Name] is not responding. [Wait] [Force Quit]"`.
  - Safely kills unresponsive threads and reclaims allocated heap memory on Force Quit.
- [x] **16. Hardware Triple Fault Prevention Shield**:
  - Dedicated Task State Segment (TSS) Task Gate for **Double Fault (Vector 8)** with an isolated stack (`double_fault_stack`).
  - Intercepts Stack Faults, GPFs (13), and Page Faults (14) before a Triple Fault reset can occur.
  - Gracefully recovers the operating system and terminates the faulty task instead of crashing the machine.
- [x] **17. Brand-New Cinematic Boot Screen**:
  - High-res animated ArchaOS logo with glowing energy pulse.
  - Hardware diagnostic boot checklist (`[ OK ] Memory`, `[ OK ] PCI Bus`, `[ OK ] Multitasking`, `[ OK ] Audio`).
  - Seamless fade transition into the desktop environment or CLI shell.

### VI. Productivity, Developer & Creative Apps
- [x] **18. Markdown Document Reader (`mdview`)**:
  - Formatted GUI and CLI reader for `.md` files rendering styled headings, bold/italic text, code fences, blockquotes, and file links.
- [x] **19. Enhanced Painter**:
  - Geometric vector tools (lines, rectangles, filled boxes, circles/ellipses).
  - Eyedropper color picker, color tolerance flood-fill, and rectangular selection/stamp tool.
- [x] **20. Interactive Fullscreen Hex Editor (`hexedit`)**:
  - Dual-pane hex bytes and ASCII column editor with live cursor navigation, byte search, and in-place file/RAM patching.
- [x] **21. Terminal Multiplexer & Split Screen (`archmux`)**:
  - Split CLI/GUI terminal panes horizontally and vertically (`Ctrl+B %` / `Ctrl+B "`) to multitask concurrently.
- [x] **22. Interactive Python REPL with `archaos` API**:
  - Live `>>> ` interactive shell with line editing, history, and the `import archaos` module to script graphics, sound, and networking.

### VII. Desktop Aesthetics & Customization
- [x] **23. Custom Bitmap Font Engine & Typography Switcher**:
  - Load `.psf` and custom bitmap fonts; switch desktop typography in Control Panel (*Retro Classic, Cyberpunk, Modern Sans, Pixel Mono*).
- [x] **24. Iconic Screensavers Collection**:
  - 3D Warp Starfield, Rotating Wireframe 3D Cube, and Mystify Glowing Curves with configurable idle timeout.

### VIII. Gaming & Server Services
- [x] **25. Classic DOOM Port (`doomgeneric`)**:
  - Freestanding pure-C DOOM engine running shareware DOOM at 35 FPS with Sound Blaster 16 audio: **"ArchaOS Runs DOOM!"**.

---

## 🛠️ The 3 Core Subsystem Overhauls

### 1. 🌐 Next-Gen Web Browser Overhaul
* **Non-Blocking Asynchronous TCP/HTTP I/O**:
  * Decouple network packet transmission and reception from the main GUI thread.
  * Downloading files or loading websites runs asynchronously in the background so the cursor, desktop, and other windows never freeze.
* **HTML Table, Row & Column Layout Engine**:
  * Real box-model flow: properly size, wrap, and separate block elements (`<div>`, `<p>`, `<table>`, `<tr>`, `<td>`, `<span>`).
  * Eliminate overlapping or squished table columns.
* **Functional CSS Styling Application**:
  * Connect NetSurf CSS engine (`libcss`) directly to the visual canvas.
  * Correctly apply foreground colors, background colors, font scaling, borders, padding, and text alignment.
* **JavaScript Engine (Duktape) DOM Bridge**:
  * Verify and wire Duktape JS bindings for `document.getElementById`, event listeners (`onclick`), timer events, and dynamic DOM updates.

### 2. 💻 App Studio 2.0 & Real Language Compilers
* **Real C Compiler / Interpreter Engine**:
  * Replace toy pattern-matching with a true recursive-descent C parser/evaluator.
  * Supports variables (`int`, `char`, pointers), expressions (`+`, `-`, `*`, `/`, `%`), conditionals (`if`/`else`), loops (`for`, `while`), arrays, and user-defined functions.
* **Full MicroPython Runtime Support**:
  * Support multi-line Python programs with functions (`def`), lists, dictionaries, string manipulations, and error stacktraces.
* **Integrated IDE Workspace**:
  * Code editor with line numbers, syntax highlighting, dual-pane console output, and RAM VFS file browser drawer.
  * One-click **Run (`F5`)** and **Build** actions.

### 3. 🖥️ Next-Gen GUI Desktop Environment
* **True-Color Visual Polish**:
  * True 32-bit ARGB compositor with soft alpha-blended drop shadows and active window perimeter glows.
* **ProcessBar 2.0 (Taskbar)**:
  * Integrated dual-workspace pager (`[ 1 ] [ 2 ]`), running window tabs with pixel-art app badges, and system tray (clock, master volume, CPU load meter).
* **Categorized Start Menu**:
  * Redesigned multi-column menu organized into *Accessories, Development, Media, System, Games*, with recent files and quick power actions.

---

## 📅 Phased Execution Roadmap

| Phase | Focus Area | Key Deliverables |
|---|---|---|
| **Phase 1** | **Kernel Hardening & Stability** | ✅ COMPLETE: Hardware Triple Fault Prevention Shield (Double Fault TSS), Preemptive Multitasking & Scheduler, Not Responding Watchdog |
| **Phase 2** | **Memory & Execution Engine** | ✅ COMPLETE: x86 Paging MMU, Ring 3 User Mode, Syscalls (`int 0x80`), Standalone ELF32 Loader |
| **Phase 3** | **Display & Visual Core** | ✅ COMPLETE: High-Res VESA VBE 640×480/800×600 true-color compositor, Brand-New Cinematic Boot Screen, Font Engine |
| **Phase 4** | **The Signature Hardware Monitor** | ✅ COMPLETE: **CoreView (Silicon Monitor)**: Live CPU registers in binary+hex with bit-flip pulse, RAM waterfall, GPU beam tracker |
| **Phase 5** | **Audio & Storage Foundation** | ✅ COMPLETE: Sound Blaster 16 & AC97 driver, Desktop event sounds, Multiboot TarFS RAM Initrd auto-extraction |
| **Phase 6** | **Subsystem Overhauls** | ✅ COMPLETE: Web Browser async I/O & HTML/CSS/JS, App Studio 2.0 real C/Python engine |
| **Phase 7** | **Desktop Experience & Utilities** | ✅ COMPLETE: Free-form icons, Right-click context menus, Dual Desktops (`Ctrl+1`/`Ctrl+2`), Spotlight QuickRunner (`Alt+Space`), Global Clipboard, Screensavers (Cube/Mystify/Starfield) |
| **Phase 8** | **Apps & Gaming Climax** | ✅ COMPLETE: `hexedit` (dual-pane), `mdview` (markdown), Enhanced Painter (9 tools), `archmux` (multiplexer), Python REPL CLI (`python`), Classic DOOM port |
