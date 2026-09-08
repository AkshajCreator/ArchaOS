# System Architecture

ArchaOS is structured as a single-address-space **32-bit x86 Monolithic Kernel** engineered from bare metal up. All subsystems—memory management, device drivers, virtual filesystem, networking, and graphics—reside within Ring 0 for maximum execution speed, zero context-switch overhead, and direct hardware accessibility.

---

## 🏗️ Kernel Initialization Sequence

When the machine boots from the bootloader (Multiboot 1 compliant), the entry point executes:

1. **GDT Setup**: Loads a flat 32-bit Global Descriptor Table (`boot.asm`) configuring 4GB code and data segments at Ring 0.
2. **IDT & Exception Handlers**: Remaps the dual 8259 PICs (`0x20-0x27` and `0x28-0x2F`) to vectors `32-47` to avoid conflicts with CPU exception vectors (`0-31`), registering assembly stubs for all 256 interrupt gates.
3. **Timer & Scheduler (PIT 8254)**: Configures PIT Channel 0 in Rate Generator mode (Square wave) running at 100 Hz (`~10ms` quantum). The timer drives the live uptime counter, sleep waits, and the background music/WAV audio scheduler.
4. **Memory Subsystem**: Probes physical RAM via Multiboot memory map tables and initializes the physical page frame allocator and kernel heap (`kmalloc`/`kfree`).
5. **Hardware Probing**:
   - Enumerates the **PCI Bus** to identify network cards (Realtek RTL8139) and IDE controllers.
   - Probes **PS/2 Controller** (`0x60`, `0x64`) to configure dual-channel keyboard (IRQ 1) and auxiliary mouse (IRQ 12) with 3-byte packet streaming.
   - Detects **Serial UART 16550** on `COM1` (`0x3F8`) and `COM2` (`0x2F8`) for headless logging.
6. **VFS Population**: Initializes the in-memory Virtual Filesystem (RAM disk), populating core system nodes (`/bin`, `/etc`, `/new`, `/general`, `/archaos.conf`).
7. **Interactive Shell / GUI Entry**: Drops into the `Arc/>` shell or boots the graphical desktop environment.

---

## 🧠 Memory Management Subsystem

### Ephemeral In-Memory Architecture (RAM Disk)
ArchaOS implements a strict **Zero Persistent Disk Footprint** policy:
- The root filesystem (`VFS`) is mounted completely inside dynamic system RAM.
- Persistent hard disks (ATA/IDE) are probed in read-only/status detection mode and are never formatted, mounted, or modified.
- Rebooting the machine returns the operating system to a completely pristine initial state.

### Kernel Heap (`kmalloc`)
- Implements a block-based dynamic memory allocator with header tags tracking allocation sizes and free lists.
- Supports runtime allocation and freeing across all GUI windows, browser DOM trees, network packet buffers, and pipeline streams.

---

## ⚡ Interrupt Handling & Hardware Drivers

| IRQ | Vector | Device | Driver Architecture |
|---|---|---|---|
| **IRQ 0** | 32 | PIT 8254 Timer | 100 Hz interrupt-driven audio playback engine & system clock |
| **IRQ 1** | 33 | PS/2 Keyboard | Scancode set 1 parser with ANSI escape generation and modifier state |
| **IRQ 11** | 43 | RTL8139 NIC | Ring buffer packet reception and ring transmission over PCI |
| **IRQ 12** | 44 | PS/2 Mouse | 3-byte stream parser with hardware delta clamping & button tracking |
| **IRQ 14** | 46 | Primary ATA | PIO mode 28-bit LBA sector reading and disk geometry detection |

---

## 📺 Dual-Mode Video Architecture

ArchaOS supports two distinct graphics execution pipelines:

### 1. Legacy VGA Mode 13h & Text Mode
- **Text Mode**: Standard 80×25 text memory buffer at physical address `0xB8000` with 16 hardware colors, used by the interactive CLI and full-screen nano editor.
- **Mode 13h**: Direct hardware register manipulation of VGA CRT/Sequencer controllers (`0x3C0-0x3DA`) to establish a planar 320×200 256-color palette buffer at `0xA0000`. Double-buffered via a 64,000-byte in-memory offscreen backbuffer with zero tearing.

### 2. Modern UEFI GOP (Graphics Output Protocol)
- For modern laptops and UEFI Class 3 firmware (no CSM/Legacy BIOS), ArchaOS utilizes the Multiboot 2 / GOP 32-bit linear framebuffer driver.
- Supports high-resolution true-color rendering (RGBA 8888) with automated fallback to Mode 13h when booting in standard legacy virtualization.
