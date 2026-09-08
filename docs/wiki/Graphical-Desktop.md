# Graphical Desktop (Mode 13h)

ArchaOS features an authentic, retro graphical desktop environment running in hardware **VGA Mode 13h (320×200, 256 indexed colors)**. The entire desktop is rendered using a **100% double-buffered backbuffer** (`64,000` bytes) mapped to physical video memory at `0xA0000`, eliminating screen tearing, flickering, and mouse cursor trails.

---

## 🪟 Window Manager Architecture

### Features & Controls
- **Double-Buffered Rendering**: Every frame is composed off-screen and blasted to video memory in a single block copy during the vertical retrace interval.
- **Dynamic Z-Ordering**: Clicking any window immediately raises it to the top of the depth stack.
- **Window Snapping**: Dragging a window to the extreme left or right edge of the screen snaps it to a 50/50 split-screen view (`160×187`).
- **Maximize & Fullscreen**: Double-clicking a window's title bar or dragging to the top edge expands it to full desktop boundaries (`320×187`), preserving the bottom ProcessBar.
- **Multi-Tier Ambient Drop Shadows**: All visible windows cast a soft 3-pixel dark alpha shadow onto underlying windows and desktop wallpapers.
- **Active Cyan Perimeter Glow**: The currently active and focused window displays an electric cyan border glow, providing instant visual focus confirmation.

---

## 🗂️ Desktop Grid & Icon System

ArchaOS v0.5 introduces a **symmetric 6-column × 3-row desktop grid** spanning all 320 horizontal screen pixels. All 18 slots are seamlessly occupied with no awkward gaps:

| Row | Col 0 | Col 1 | Col 2 | Col 3 | Col 4 | Col 5 |
|---|---|---|---|---|---|---|
| **Row 0** | Web Browser | Terminal CLI | App Studio | File Manager | Notepad | Painter |
| **Row 1** | Calculator | Minesweeper | Snake | Control Panel | Task Manager | Image Viewer |
| **Row 2** | Audio Player | System Info | `/new` Manual | `/general` Manual | `/archaos.conf` | `/readme.txt` |

### MIME Association & File Launching
Double-clicking files on the desktop or in File Manager automatically dispatches them to their registered handler:
- `.txt`, `.conf`, `.md` $\rightarrow$ Notepad / GNU nano
- `.bmp`, `.ppm` $\rightarrow$ Image Viewer
- `.wav`, `.mp3` $\rightarrow$ Docked Media Player / Audio Synthesizer
- `.py` $\rightarrow$ MicroPython Interpreter
- `.c` $\rightarrow$ Tiny C Compiler (TCC) Studio

---

## 🛠️ Built-in GUI Applications

| Application | Description |
|---|---|
| **Web Browser** | NetSurf-powered HTML rendering, JavaScript engine, Speed Dial bookmarks bar, and integrated docked media player. |
| **Terminal CLI** | Embedded graphical terminal window running interactive shell commands directly inside Mode 13h. |
| **App Studio** | Developer IDE for writing, compiling, and running native C scripts via Tiny C Compiler (TCC) and MicroPython. |
| **File Manager** | VFS tree explorer with New File, New Directory, Rename, Delete, and item property dialogs. |
| **Notepad** | Vintage text editor supporting full ASCII text entry, multi-line navigation, and RAM disk saving. |
| **Painter** | 256-color raster paint utility featuring pencil, brush, eraser, flood fill, color palette picker, and BMP export. |
| **Calculator** | 4-function pocket calculator with visual button feedback and operator precedence. |
| **Minesweeper** | Classic 8×8 minefield with PRNG generation, left-click reveal, right-click flag, and win/loss state detection. |
| **Snake** | High-refresh-rate arcade snake game with score tracking, apple spawning, speed escalation, and sound effects. |
| **Control Panel** | Personalize desktop visual themes (4 options) and wallpapers (4 styles); automatically persists to `archaos.conf`. |
| **Task Manager** | Real-time process and window inspector showing memory usage, window states, and process kill buttons. |
| **Image Viewer** | High-speed BMP image viewer with canvas scaling and image inspection tools. |

---

## 🎨 Themes & Wallpapers

Control Panel provides 4 built-in retro palettes and 4 procedural wallpaper backgrounds:

### Visual Themes
1. **Classic Teal**: Authentic Windows 95 cyan/teal backdrop with cool gray window chrome.
2. **Win31 Navy**: Deep cobalt blue backdrop with high-contrast steel borders.
3. **Matrix Green**: Phosphor green-on-black aesthetic inspired by vintage mainframe terminals.
4. **Amber Gold**: Warm monochrome amber CRT glow.

### Procedural Wallpapers
- **Solid**: Clean, distraction-free solid theme background color.
- **Starfield**: Procedurally generated 256-star cosmos field.
- **Cyber Grid**: Isometric perspective grid receding into the horizon.
- **Sunset Lines**: 80s synthwave-style horizontal color gradient stripes.
