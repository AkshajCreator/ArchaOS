# Web Browser & Networking

ArchaOS includes a complete bare-metal network stack and an embedded graphical Web Browser powered by the **NetSurf** rendering engine, an in-tree **JavaScript runtime**, and **NanoSVG** vector graphics support.

---

## 🌐 Network Subsystem Architecture

The ArchaOS networking pipeline is built directly into the kernel:

```
+-----------------------------------------------------------+
| Applications: Web Browser, curl, wget, ping               |
+-----------------------------------------------------------+
| Transport Layer: TCP (Stateful), UDP                      |
+-----------------------------------------------------------+
| Security Layer: BearSSL TLS 1.3 (HTTPS Encryption)        |
+-----------------------------------------------------------+
| Internet Layer: IPv4, ARP, ICMP, DNS Resolver            |
+-----------------------------------------------------------+
| Link Layer: Realtek RTL8139 Fast Ethernet PCI Driver      |
+-----------------------------------------------------------+
```

### Realtek RTL8139 PCI Driver
- Auto-probed via PCI vendor ID `0x10EC` and device ID `0x8139`.
- Maps memory-mapped I/O (MMIO) and I/O port spaces.
- Utilizes continuous DMA ring buffers for asynchronous packet reception and transmission.

### BearSSL Embedded TLS Engine
- Custom-tailored build of BearSSL compiled with `-m32 -fno-builtin`.
- Provides full modern TLS 1.3 handshake negotiation, certificate parsing, and AES-GCM / ChaCha20-Poly1305 cipher suites directly on bare metal without any glibc dependency.

---

## 🌍 Graphical Web Browser

The ArchaOS Web Browser runs as a first-class window inside Mode 13h:

### Key Capabilities
- **HTML & CSS Rendering**: NetSurf core formats headings, tables, links, bold/italic text, and image placeholders.
- **JavaScript Engine**: Executes lightweight script logic, page interactions, and dynamic UI updates.
- **NanoSVG Vector Graphics**: Decodes and renders crisp SVG vector assets directly to the Mode 13h framebuffer.
- **Tabbed Browsing**: Open multiple parallel web pages with `Ctrl+Tab` switching.
- **Speed Dial Bookmarks**: Instant single-click navigation bar with presets:
  - `Home`: Local system dashboard and offline manual.
  - `DuckDuckGo`: Clean web search engine.
  - `Wikipedia`: Free online encyclopedia.
  - `Media`: Local media player interface.

### Context-Aware Search Engine Routing
- Searching from the address bar routes queries intelligently:
  - General queries are dispatched to **DuckDuckGo**.
  - Searches initiated while browsing Wikipedia are routed directly into **Wikipedia's native search engine** (`https://en.m.wikipedia.org/w/index.php?search=...`).
  - Search-intent validation prevents non-search page elements (such as collapsible section toggles or hamburger menus) from mistakenly triggering web searches.

---

## 🎵 Docked In-Browser Media Player

The browser includes a docked multimedia player bar at the base of its window:
- **Playback Controls**: Play (`>`), Pause (`||`), Stop, and Seek.
- **Scrubber Gauge**: Visual playback progression bar with hover timestamp preview.
- **Dual Timecodes**: Current elapsed time and total duration (`01:23 / 03:45`).
- **PIT Audio Multitasking**: Audio is played using a ~100 Hz timer interrupt on the Programmable Interval Timer (PIT Channel 0). You can listen to synthesized melodies or WAV chiptunes continuously in the background while multitasking across other windows or typing in the CLI.
