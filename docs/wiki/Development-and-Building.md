# Development & Building Guide

This guide walks you through setting up a build environment, compiling ArchaOS from source, testing in virtual machines, and booting on real x86 hardware.

---

## 🧰 Build Prerequisites

Building ArchaOS requires a standard Linux development environment (Ubuntu/Debian, Fedora, or Arch Linux):

```bash
# Ubuntu / Debian
sudo apt-get update
sudo apt-get install -y build-essential nasm xorriso grub-pc-bin grub-common mtools qemu-system-x86
```

### Core Toolchain Requirements
- **GCC**: With 32-bit compilation support (`gcc-multilib` / `-m32`).
- **NASM**: Netwide Assembler (`nasm`) for 32-bit protected mode boot stubs and interrupt handlers.
- **GNU Linker (`ld`)**: Target `elf_i386`.
- **xorriso & grub-mkrescue**: Creates bootable El Torito / ISOHybrid images.

---

## 🔨 Compiling the Kernel

Clone the repository and build using the provided Makefile:

```bash
git clone https://github.com/AkshajCreator/ArchaOS.git
cd ArchaOS

# Clean existing build artifacts
make clean

# Build the complete kernel binary and bootable ISO
make
```

Upon completion, the build generates:
- `build/kernel.bin`: 32-bit Multiboot ELF kernel executable.
- `ArchaOS.iso`: Hybrid bootable ISO image ready for emulation or USB flashing.

---

## 🧪 Emulation & Testing

### Running with QEMU (Recommended)
Launch ArchaOS with full networking, PC speaker audio emulation, and 128MB RAM:

```bash
qemu-system-i386 -cdrom ArchaOS.iso -m 128M -soundhw pcspk -net nic,model=rtl8139 -net user
```

### Running in VirtualBox
1. Create a new VM:
   - **Type**: Other
   - **Version**: Other/Unknown (32-bit)
   - **Memory**: 128 MB (or 256 MB)
2. Storage: Mount `ArchaOS.iso` into the virtual CD/DVD drive.
3. Network: Set Adapter 1 to **Bridged** or **NAT**, with adapter type **PCnet-FAST III** or **Intel PRO/1000 MT Desktop**.
4. Audio: Enable Audio (Intel HD Audio or Sound Blaster 16).

---

## 💾 Booting on Bare-Metal Hardware

ArchaOS is packaged as an **ISOHybrid** image with dual UEFI GOP and Legacy BIOS support:

### Flashing to a USB Drive
> ⚠️ **Warning**: Ensure you select the correct drive letter (`/dev/sdX`). Writing to the wrong disk will overwrite existing data.

```bash
sudo dd if=ArchaOS.iso of=/dev/sdX bs=4M conv=fsync status=progress
```

### Hardware Compatibility
- **Legacy BIOS / CSM**: Full support for older Intel Core 2 Duo, Pentium, and legacy PCs.
- **Pure UEFI Class 3**: Full compatibility with modern Intel (10th–14th Gen) and AMD Ryzen laptops without CSM through our GOP 32-bit linear framebuffer driver.

---

## 🐍 Writing Custom Scripts in ArchaOS

ArchaOS includes two native scripting runtimes:

### 1. MicroPython
Create a python script using `nano /test.py`:
```python
# test.py - MicroPython on ArchaOS
for i in range(5):
    print("Hello from ArchaOS Python! Count:", i)
```
Run it from the shell or App Studio:
```bash
python /test.py
```

### 2. Tiny C Compiler (TCC)
ArchaOS can compile and execute C programs on the fly directly in memory:
```c
/* app.c */
int main() {
    print("Compiled on bare metal ArchaOS!\n");
    return 0;
}
```
Run via `run /app.c` in the CLI or compile in App Studio.
