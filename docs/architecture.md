# ArchaOS Architecture & Component Separation

This document describes the architectural layout of ArchaOS, the separation between kernel and driver layers, and the roadmap for transitioning from an in-kernel monolithic execution model toward standalone Ring 3 userland processes.

---

## 1. Architectural Layers

ArchaOS is structured into four functional layers:

```
+--------------------------------------------------------------+
| Layer 4: User-Space Programs (Target Architecture)          |
|   /bin/sh, /bin/nano, /bin/coreview, /bin/ls, /bin/cat...    |
+--------------------------------------------------------------+
                               |  int 0x80 (syscall interface)
+--------------------------------------------------------------+
| Layer 3: Kernel Subsystems                                   |
|   Virtual File System (RAM disk), TCP/IP Stack, Compositor   |
+--------------------------------------------------------------+
| Layer 2: Device Drivers                                      |
|   PS/2 Keyboard (keyboard.c), PS/2 Mouse, VGA/VESA Graphics |
|   16550 UART Serial, IDE/ATA Controller, Intel E1000 NIC    |
+--------------------------------------------------------------+
| Layer 1: Core Kernel                                         |
|   GDT, IDT & ISRs, Paging (VMM), Physical Memory (PMM),     |
|   Heap Allocator (kmalloc), Preemptive Scheduler (task.c),   |
|   Syscall Dispatcher (syscall.c), ELF32 Loader (elf.c)       |
+--------------------------------------------------------------+
| Layer 0: Hardware Platform (x86 32-bit Protected Mode)       |
+--------------------------------------------------------------+
```

---

## 2. Core Kernel Components

The core kernel lives in `src/` and provides hardware abstraction and resource multiplexing:

- **Boot Entry (`boot.asm`, `linker.ld`)**: Sets up the Multiboot header, enables protected mode, aligns the stack, initializes the FPU, and transfers control to `kernel_main()`.
- **Descriptor Tables (`gdt.c`, `idt.c`, `isr_stubs.asm`)**: Configures flat 4 GB kernel and user code/data segments, remaps the legacy 8259 PIC vectors to 0x20-0x2F, and installs exception handlers (ISR 0-31) and hardware IRQs (IRQ 0-15).
- **Memory Management (`mm.c`, `vmm.c`)**:
  - `vmm.c`: Physical frame bitmap allocator (PMM) and two-level paging directory (VMM). Parses genuine Multiboot E820 memory map tables (`mmap_addr`, `mmap_length`), protects critical low memory (0-1MB), BIOS, and kernel code regions, and marks only validated usable RAM frames as free.
  - `mm.c`: Dynamic on-demand kernel heap allocator (`kmalloc`, `kfree`, `krealloc`). Backed directly by PMM page frame allocations via `pmm_alloc_frame()` and `vmm_map_page()`, eliminating static BSS heap constraints and scaling to available RAM up to 512MB+.
  - **Higher-Half Roadmap**: Transition specification defined for `0xC0000000` (3 GB virtual base). Kernel space occupies PDE entries 768–1023, reserving lower virtual addresses (0x00000000–0xBFFFFFFF) exclusively for isolated Ring 3 userland task contexts.
- **Interrupts & Exceptions (`idt.c`, `isr_stubs.asm`)**:
  - Full CPU exception vector coverage (ISRs 0 through 31) with proper architectural error-code handling for `#DF` (8), `#TS` (10), `#NP` (11), `#SS` (12), `#GP` (13), `#PF` (14), `#AC` (17), `#CP` (21), `#VC` (29), and `#SX` (30).
  - PIC spurious IRQ 7 and IRQ 15 handling via In-Service Register (ISR) inspection to prevent PIC state corruption.
- **Multitasking & Cooperative Sleep (`task.c`, `task.h`, `pit.c`)**:
  - Maintains task control blocks (TCBs) with saved processor state (`task_registers_t`).
  - Preemptive context switching driven by the PIT timer (IRQ0 at 1000 Hz).
  - Task state lifecycle: Ready, Running, Blocked, Sleeping, Terminated.
  - Non-busy cooperative yielding sleeps (`task_sleep`) integrated into all system timing calls.
- **Syscall Dispatch (`syscall.c`, `syscall.h`)**: Handles software interrupts via `int 0x80` with arguments passed through CPU registers (`eax`=syscall number, `ebx`=arg1, `ecx`=arg2, `edx`=arg3).
- **Executable Binary Loader (`elf.c`, `elf.h`)**: Validates ELF32 headers, parses `PT_LOAD` program headers, allocates virtual memory pages, copies code and data segments, and prepares the initial stack.

---

## 3. Centralized Standard Libraries & Drivers

Previous versions had duplicated string helpers and ad-hoc keyboard loops scattered throughout modules. These have been consolidated:

### 3.1 Standard C Routines (`string.c`, `string.h`)
- Single, authoritative implementation of ISO C string and memory primitives (`strlen`, `strcpy`, `strncpy`, `strcat`, `strncat`, `strcmp`, `strncmp`, `strchr`, `strstr`, `memset`, `memcpy`, `memmove`, `memcmp`, `atoi`, `itoa`).
- Shared across all kernel modules, filesystems, and drivers without local re-implementations.

### 3.2 PS/2 Keyboard Subsystem (`keyboard.c`, `keyboard.h`)
- Centralized translation table for standard US QWERTY keyboard layout.
- Decouples raw hardware scancode capture (via IRQ1) from consumer input processing.
- Handles Shift, CapsLock, Ctrl, Alt state tracking and provides non-blocking polling (`keyboard_has_char()`), blocking wait (`keyboard_getchar()`), and scancode decoding (`keyboard_get_scancode()`).

---

## 4. Current Built-ins vs. Userland Program Targets

ArchaOS currently executes utility commands as compiled-in kernel routines dispatched via `kernel_execute_command()` in `src/kernel.c`. The ongoing architectural roadmap involves migrating these utilities into standalone userland binaries executed via `exec()` and `elf.c`:

| Program | Current Implementation | Target Userland Binary | Required Syscalls |
|---|---|---|---|
| **Shell** | In-kernel loop in `vga.c` / `kernel.c` | `/bin/sh` | `read`, `write`, `exec`, `fork`/`spawn`, `waitpid` |
| **Core Utilities** | `ls`, `cat`, `echo`, `mkdir`, `rm`, `cp`, `mv`, `grep`, `wc`, `hexdump` in `kernel.c` | `/bin/ls`, `/bin/cat`, `/bin/echo`, `/bin/rm`, etc. | `read`, `write`, `open`, `close`, `stat`, `getdents` |
| **Text Editor** | `src/editor.c` (`nano`) | `/bin/nano` | `read`, `write`, `open`, `close`, `ioctl`/terminal control |
| **Hardware Monitor** | `src/coreview.c` | `/bin/coreview` | `sys_sysinfo`, `sys_mempoll`, `sys_regdump` |
| **System Info & Games** | `neofetch`, `matrix`, `snake` in `kernel.c` | `/bin/neofetch`, `/bin/matrix`, `/bin/snake` | `write`, `sleep`, `yield`, `read` |
| **Window Manager** | `src/gui.c` | `/bin/gui` (Display Server) | Framebuffer memory map (`mmap`), mouse/keyboard event queue |

---

## 5. Syscall Interface Specification (`int 0x80`)

The userland runtime interface currently implements the following system call table in `src/syscall.c`:

| Syscall # | Name | Signature | Description |
|---|---|---|---|
| `0x01` | `SYS_EXIT` | `void sys_exit(int status)` | Terminates current process and reclaims resources |
| `0x02` | `SYS_FORK` | `int sys_fork(void)` | Clones execution context |
| `0x03` | `SYS_READ` | `int sys_read(int fd, char *buf, size_t count)` | Reads bytes from stdin or file descriptor |
| `0x04` | `SYS_WRITE` | `int sys_write(int fd, const char *buf, size_t count)` | Writes bytes to stdout, stderr, or file descriptor |
| `0x05` | `SYS_OPEN` | `int sys_open(const char *path, int flags)` | Opens file path in VFS |
| `0x06` | `SYS_CLOSE` | `int sys_close(int fd)` | Releases file descriptor |
| `0x07` | `SYS_EXEC` | `int sys_exec(const char *path, char **argv)` | Loads and executes ELF32 binary |
| `0x08` | `SYS_GETPID` | `int sys_getpid(void)` | Returns process identifier of calling task |
| `0x09` | `SYS_YIELD` | `void sys_yield(void)` | Voluntarily yields CPU timeslice |
| `0x0A` | `SYS_SLEEP` | `void sys_sleep(uint32_t ms)` | Blocks calling task for duration in milliseconds |
| `0x0B` | `SYS_SBRK` | `void *sys_sbrk(intptr_t increment)` | Adjusts userland heap break point |
