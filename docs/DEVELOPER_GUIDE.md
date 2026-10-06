# ArchaOS Application Developer Guide

Welcome to the ArchaOS developer documentation. This guide provides everything required for developers and AI code assistants (such as ChatGPT) to write, compile, and run C applications on ArchaOS.

ArchaOS runs applications in **Ring 3 user space** backed by a freestanding C library. You write standard C code, and the ArchaOS toolchain automatically handles stack setup, 16-byte ABI alignment, system calls (`int 0x80`), and ELF formatting.

---

## 1. Quick Start

### Basic Program Template
Every ArchaOS application uses a standard C entry point:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    printf("Hello from ArchaOS!\n");
    if (argc > 1) {
        printf("Received argument: %s\n", argv[1]);
    }
    return 0;
}
```

You can include standard headers (`<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<unistd.h>`, `<fcntl.h>`) or simply `#include <archaos.h>` which includes all of them.

---

## 2. Standard C Headers & Available APIs

ArchaOS provides clean, standard-compliant implementations of common C standard library functions:

### Standard I/O (`<stdio.h>`)
| Function | Signature | Description |
| :--- | :--- | :--- |
| `printf` | `int printf(const char *fmt, ...);` | Formatted output. Supports `%s`, `%d`, `%u`, `%x`, `%c`, `%p`, `%%`. |
| `puts` | `int puts(const char *s);` | Writes string followed by a newline to stdout. |
| `putchar` | `int putchar(char c);` | Writes a single character to stdout. |
| `getchar` | `int getchar(void);` | Reads a single character from stdin (blocking). |

### General Utilities & Memory (`<stdlib.h>`)
| Function | Signature | Description |
| :--- | :--- | :--- |
| `malloc` | `void *malloc(size_t size);` | Dynamic heap allocation (first-fit with coalescing). |
| `free` | `void free(void *ptr);` | Releases previously allocated heap block. |
| `exit` | `void exit(int status);` | Terminates process and returns exit code to kernel. |
| `atoi` | `int atoi(const char *str);` | Converts string to 32-bit signed integer. |
| `itoa` | `char *itoa(int value, char *str, int base);` | Converts integer to string (base 2..36). |

### String & Buffer Operations (`<string.h>`)
| Function | Signature | Description |
| :--- | :--- | :--- |
| `strlen` | `size_t strlen(const char *s);` | Computes string length excluding null byte. |
| `strcmp` | `int strcmp(const char *s1, const char *s2);` | Compares two null-terminated strings. |
| `strncmp` | `int strncmp(const char *s1, const char *s2, size_t n);` | Compares up to `n` characters. |
| `strcpy` | `char *strcpy(char *dest, const char *src);` | Copies source string into destination buffer. |
| `strncpy` | `char *strncpy(char *dest, const char *src, size_t n);` | Copies up to `n` characters. |
| `memset` | `void *memset(void *s, int c, size_t n);` | Fills memory buffer with constant byte `c`. |
| `memcpy` | `void *memcpy(void *dest, const void *src, size_t n);` | Copies `n` bytes between buffers. |

### POSIX System Calls & Process Control (`<unistd.h>`, `<fcntl.h>`)
| Function | Signature | Description |
| :--- | :--- | :--- |
| `open` | `int open(const char *path, int flags);` | Opens file (e.g. `O_RDONLY`, `O_WRONLY`). |
| `read` | `int read(int fd, void *buf, size_t count);` | Reads bytes from file descriptor. |
| `write` | `int write(int fd, const void *buf, size_t count);` | Writes bytes to file descriptor. |
| `close` | `int close(int fd);` | Closes open file descriptor. |
| `sleep` | `void sleep(uint32_t ms);` | Non-busy sleep for `ms` milliseconds. |
| `yield` | `void yield(void);` | Yields remainder of current CPU time slice. |
| `getpid` | `int getpid(void);` | Returns process ID (PID) of current task. |

---

## 3. How to Compile Applications

ArchaOS supports two convenient ways to compile applications:

### Method A: Integrated Build (Automatic Discovery)
1. Place your C source file in `user/bin/` (e.g., `user/bin/myapp.c`).
2. Run `make` in the repository root:
   ```bash
   make
   ```
3. The build system will automatically:
   - Compile `myapp.c` with 32-bit freestanding flags (`-m32 -ffreestanding -nostdlib`).
   - Link with `user/crt0.o` (process entry point) and `user/libc.o` into `build/bin/myapp.elf`.
   - Embed the binary into the bootable ISO under `/bin/myapp.elf`.

### Method B: Standalone Compiler Tool (`archaos-cc`)
If you want to compile single or multiple C source files outside the tree:

**Single file compilation:**
```bash
./tools/archaos-cc myapp.c -o myapp.elf
```

**Multi-file compilation with dependencies:**
```bash
./tools/archaos-cc main.c math.c utils.c -I./include -o myapp.elf
```
`archaos-cc` will compile each `.c` source into an independent 32-bit freestanding object file, link them with `crt0.o`, `libc.o`, and `gui.o`, and output a unified Ring 3 ELF binary ready for ArchaOS.

### Method C: In-Kernel App Studio Multi-File C Engine
ArchaOS provides an integrated development environment inside the GUI:
1. Open **App Studio** from the Desktop or Start Menu (shortcut `F1`).
2. Write or load your C program (e.g. `/main_calc.c`).
3. Include dependent `.c` files using standard `#include`:
   ```c
   #include "math_lib.c"

   int main() {
       int s = square(7);
       int total = add(s, 20);
       printf("total = %d\n", total);
   }
   ```
4. The in-kernel engine resolves dependencies dynamically from the VFS (`math_lib.c` or `/math_lib.c`), parses and binds dependent functions, and executes them with full scope preservation.
5. Press **F5** or **Ctrl+R** (or click **[Run]**) to execute. The output console window pops up displaying real-time program execution.

---

## 4. Running Your Application

Inside ArchaOS, you do **not** need to type `run` or specify paths. Simply type your application's name in the shell:

```text
Arc/> myapp
Hello from ArchaOS!

Arc/> myapp test_argument 42
Hello from ArchaOS!
Received argument: test_argument
```

---

## 5. Complete Example Applications

### Example 1: Command-Line Argument Parser
```c
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    printf("ArchaOS Argument Echo\n");
    printf("Total arguments: %d\n", argc);

    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }
    return 0;
}
```

### Example 2: Math Calculator
```c
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 4) {
        printf("Usage: %s <num1> <op> <num2>\n", argv[0]);
        return 1;
    }

    int a = atoi(argv[1]);
    char op = argv[2][0];
    int b = atoi(argv[3]);
    int result = 0;

    if (op == '+') result = a + b;
    else if (op == '-') result = a - b;
    else if (op == '*') result = a * b;
    else if (op == '/') {
        if (b == 0) {
            printf("Error: Division by zero\n");
            return 1;
        }
        result = a / b;
    } else {
        printf("Error: Unknown operator '%c'\n", op);
        return 1;
    }

    printf("%d %c %d = %d\n", a, op, b, result);
    return 0;
}
```

### Example 3: File Reader (`cat` clone)
```c
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/readme.txt";

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("Error: cannot open '%s'\n", path);
        return 1;
    }

    char buf[128];
    int n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    close(fd);
    return 0;
}
```

---

## 6. GUI Application Programming (Widget Toolkit & 2D Canvas)

ArchaOS features a full-featured bare-metal GUI platform and widget toolkit allowing applications to create real desktop windows with **Buttons**, **Text Boxes**, **Checkboxes**, **Progress Bars**, **Labels**, and direct 2D canvas drawing.

Include `<gui.h>` or `<archaos.h>` to access the GUI toolkit.

### 6.1 Widget Construction & Management
| Function | Description |
| :--- | :--- |
| `int gui_create_window(const char *title, int w, int h)` | Creates a new desktop window with client interior size `w × h`. Returns `win_id >= 0` on success. |
| `void gui_close_window(int win)` | Closes the window and frees its registered widgets and canvas memory. |
| `gui_widget_t *gui_add_button(int win, int x, int y, int w, int h, const char *text)` | Adds a 3D beveled clickable push button with pressed-in visual state. |
| `gui_widget_t *gui_add_textbox(int win, int x, int y, int w, int h, const char *initial_text)` | Adds an interactive text box with focus border highlight, typing, backspace, and cursor (`\|`). |
| `gui_widget_t *gui_add_checkbox(int win, int x, int y, const char *label, int checked)` | Adds a clickable checkbox with an `[X]` checkmark and toggleable state. |
| `gui_widget_t *gui_add_progressbar(int win, int x, int y, int w, int h, int progress)` | Adds a progress bar with dynamic percentage fill (`0..100%`). |
| `gui_widget_t *gui_add_label(int win, int x, int y, const char *text)` | Adds a formatted text label. |
| `void gui_set_progress(gui_widget_t *bar, int progress)` | Dynamically updates a progress bar's fill percentage (`0..100`). |
| `void gui_set_text(gui_widget_t *widget, const char *text)` | Dynamically updates a label, button, or textbox's text content. Automatically wipes previous bounds. |
| `int gui_poll_event(int win, gui_event_t *ev)` | Automatic event pump. Handles widget hit-testing, focus switching, cursor typing, and returns `1` when an event occurs (`0` if empty). |

### 6.2 Custom Window Icons & Author Attribution
Developers can customize their application's 7×7 pixel icon and display their name directly in the window title bar and taskbar:

| Function | Signature | Description |
| :--- | :--- | :--- |
| `gui_set_icon` | `void gui_set_icon(int win, const uint8_t icon[7][7], uint8_t color);` | Sets a custom 7×7 monochrome icon displayed on the title bar and taskbar button. `color` is a `GUI_COLOR_*` palette index. |
| `gui_set_author` | `void gui_set_author(int win, const char *author);` | Sets the developer or author name (up to 31 chars), displayed in the window title bar as `"<Title> by <Author>"`. |

#### Example: Setting Custom Icon & Author
```c
static const uint8_t my_icon[7][7] = {
    {0, 0, 1, 1, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0},
    {1, 1, 0, 1, 1, 1, 0},
    {1, 0, 0, 0, 1, 1, 1},
    {1, 1, 0, 1, 1, 1, 0},
    {0, 1, 1, 1, 1, 0, 0},
    {0, 0, 1, 1, 0, 0, 0}
};

int win = gui_create_window("Fractal Explorer", 300, 200);
if (win < 0) return 1;

gui_set_icon(win, my_icon, GUI_COLOR_LIGHT_CYAN);
gui_set_author(win, "ChatGPT");
```

### 6.3 Low-Level 2D Canvas Drawing
If your application wants to draw custom shapes, diagrams, or games directly inside the window:
| Function | Description |
| :--- | :--- |
| `void gui_clear(int win, uint8_t color)` | Fills the entire window client area with a background color. |
| `void gui_draw_rect(int win, int x, int y, int w, int h, uint8_t color)` | Draws a solid filled rectangle. |
| `void gui_draw_text(int win, int x, int y, const char *str, uint8_t color)` | Renders text using the system bitmap font. |
| `void gui_draw_pixel(int win, int x, int y, uint8_t color)` | Sets a single pixel at client coordinate `(x, y)`. |
| `void gui_update(int win)` | Blits canvas updates to the active VESA screen. |

### 6.4 Standard Color Palette Constants
ArchaOS provides predefined 256-color palette constants in `<gui.h>`:
| Constant | Color Value | Description |
| :--- | :--- | :--- |
| `GUI_COLOR_BLACK` | `0x00` | Solid Black |
| `GUI_COLOR_BLUE` | `0x01` | Deep Blue |
| `GUI_COLOR_GREEN` | `0x02` | Forest Green |
| `GUI_COLOR_CYAN` | `0x03` | Cyan |
| `GUI_COLOR_RED` | `0x04` | Crimson Red |
| `GUI_COLOR_MAGENTA` | `0x05` | Purple / Magenta |
| `GUI_COLOR_BROWN` | `0x06` | Amber / Brown |
| `GUI_COLOR_LIGHT_GRAY` | `0x07` | Default Window Client Background |
| `GUI_COLOR_DARK_GRAY` | `0x08` | Border / Shadow Gray |
| `GUI_COLOR_LIGHT_BLUE` | `0x09` | Accent / Progress Blue |
| `GUI_COLOR_LIGHT_GREEN` | `0x0A` | Vibrant Lime Green |
| `GUI_COLOR_LIGHT_CYAN` | `0x0B` | Bright Aqua |
| `GUI_COLOR_LIGHT_RED` | `0x0C` | Coral / Light Red |
| `GUI_COLOR_YELLOW` | `0x0E` | Vibrant Yellow |
| `GUI_COLOR_WHITE` | `0x0F` | Bright White |

### 6.5 Event Handling & Dispatch
The `gui_event_t` structure received from `gui_poll_event(win, &ev)`:
```c
typedef struct {
    int  type;       /* Event type (see below) */
    int  x, y;       /* Mouse coordinates relative to window interior */
    int  button;     /* Mouse button (1=Left, 2=Right) */
    char key;        /* Key ASCII character */
    int  widget_id;  /* ID of the target widget (for button click, text change, etc.) */
} gui_event_t;
```

#### Event Types:
- `GUI_EVENT_BUTTON_CLICK`: User clicked a button. Compare `ev.widget_id == my_button->id`.
- `GUI_EVENT_TEXT_CHANGE`: User typed or backspaced inside a text box. Read `my_textbox->text`.
- `GUI_EVENT_CHECKBOX_TOGGLE`: User clicked a checkbox. Inspect `my_checkbox->checked` (`0` or `1`).
- `GUI_EVENT_CLOSE`: User clicked the window frame's close button (`X`). Terminate the event loop cleanly.

### 6.6 Complete GUI Application Example
```c
#include <stdio.h>
#include <archaos.h>
#include <gui.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* 1. Create a 220x140 pixel application window */
    int win = gui_create_window("Form App", 220, 140);
    if (win < 0) return 1;

    /* Optional: Custom 7x7 icon and author attribution */
    static const uint8_t app_icon[7][7] = {
        {1,1,1,1,1,1,1},
        {1,0,0,0,0,0,1},
        {1,0,1,0,1,0,1},
        {1,0,0,1,0,0,1},
        {1,0,1,0,1,0,1},
        {1,0,0,0,0,0,1},
        {1,1,1,1,1,1,1}
    };
    gui_set_icon(win, app_icon, GUI_COLOR_YELLOW);
    gui_set_author(win, "Developer");

    /* 2. Add widgets to window */
    gui_widget_t *lbl_user = gui_add_label(win, 10, 12, "Name:");
    gui_widget_t *txt_user = gui_add_textbox(win, 55, 10, 150, 14, "Alice");
    gui_widget_t *chk_snd  = gui_add_checkbox(win, 10, 35, "Enable sound effects", 1);
    gui_widget_t *pbar     = gui_add_progressbar(win, 10, 58, 195, 10, 40);
    gui_widget_t *btn_add  = gui_add_button(win, 10, 80, 90, 18, "+20% Progress");
    gui_widget_t *btn_quit = gui_add_button(win, 115, 80, 90, 18, "Close App");
    gui_widget_t *lbl_stat = gui_add_label(win, 10, 112, "Ready. Click widgets or type!");

    int progress = 40;

    /* 3. Event Loop */
    while (1) {
        gui_event_t ev;
        if (gui_poll_event(win, &ev)) {
            if (ev.type == GUI_EVENT_CLOSE) {
                break;
            }

            if (ev.type == GUI_EVENT_BUTTON_CLICK) {
                if (ev.widget_id == btn_add->id) {
                    progress = (progress + 20) % 120;
                    if (progress > 100) progress = 100;
                    gui_set_progress(pbar, progress);
                    gui_set_text(lbl_stat, "Progress updated!");
                } else if (ev.widget_id == btn_quit->id) {
                    break;
                }
            } else if (ev.type == GUI_EVENT_CHECKBOX_TOGGLE) {
                if (ev.widget_id == chk_snd->id) {
                    gui_set_text(lbl_stat, chk_snd->checked ? "Sound: ON" : "Sound: OFF");
                }
            } else if (ev.type == GUI_EVENT_TEXT_CHANGE) {
                if (ev.widget_id == txt_user->id) {
                    gui_set_text(lbl_stat, "Typing in textbox...");
                }
            }
        }

        /* Yield CPU time slice to other tasks */
        sleep(20);
    }

    /* 4. Clean up */
    gui_close_window(win);
    return 0;
}
```

### 6.7 How to Compile and Launch Applications
- **Standalone Compile**:
  ```bash
  ./tools/archaos-cc myapp.c -o myapp.elf
  ```
- **Integrated Build**:
  Place `myapp.c` into `user/bin/` and run `make`. It will automatically be embedded into the ISO under `/bin/myapp.elf`.
- **Launching Directly from the GUI**:
  - **Start Menu**: Click `ArchaOS` at the bottom-left and select **"Run Program..."** (item 13). Enter any command or binary path (e.g. `calc`, `mandelbrot`, `/bin/demo_gui.elf`) to launch it immediately.
  - **Desktop Shortcuts**: Double-click any application shortcut icon on the desktop.
  - **File Manager**: Navigate to `/bin` in the File Manager (`Files`) and double-click `myapp.elf`.
- **Launching via Text Shell**:
  - Run CLI binaries directly by command name: `calc`, `sha256`, etc.
  - Run GUI applications while switching into GUI: `gui /bin/myapp.elf`
- **CLI Freeze Prevention & Watchdog Safety**:
  - If a GUI application is accidentally invoked in the text-mode shell (e.g., typing a GUI program name without `gui`), `gui_create_window()` safely detects that the graphical desktop is not active and returns `-1` with a warning message (`[GUI ERROR] GUI desktop is not active! Launch GUI with 'gui' first.`) so the process exits cleanly instead of freezing.
  - The shell foreground process wait loop actively monitors for **`Ctrl+C`** (`\x03`) and **`Escape`** (27). Pressing either immediately terminates any runaway or hanging process and returns control to the shell prompt.

---

## 6.8 Desktop Integration Rule: Loading Applications onto the Desktop

To make your application automatically load onto the ArchaOS Desktop with its custom title, add the following macro declaration at file scope (outside any function, near the top of your `.c` file):

```c
#include <archaos.h>
#include <gui.h>

ARCHAOS_GUI_APP("My App Title");
```

### How It Works:
1. **Zero Kernel Hardcoding**: You never have to touch a single line of kernel code or submit kernel pull requests to add your application to the GUI.
2. **Automatic Desktop Discovery**: When ArchaOS starts or updates the desktop, the compositor scans `/bin/` for binaries containing the `ARCHAOS_GUI:` marker.
3. **Clean Desktop**: Pure CLI utilities (like `cat`, `echo`, `grep`, `sh`, `sha256`) that do not declare `ARCHAOS_GUI_APP` will not clutter the desktop. Only applications declared with `ARCHAOS_GUI_APP` appear as desktop shortcuts.
4. **Custom Title Display**: The title provided in `ARCHAOS_GUI_APP("...")` is automatically rendered as the icon label on the desktop.
5. **Custom 7×7 Icon & Author Attribution**: Inside `main()`, call `gui_set_icon(win, icon, color)` and `gui_set_author(win, "Author Name")` to display your custom pixel-art icon and author credits in the window title bar and taskbar button.

---

## 7. What Developers Do NOT Need to Worry About
- **Calling conventions & ABI**: `crt0.asm` guarantees 16-byte stack alignment as required by the GCC x86 System V ABI.
- **Hardware registers & privileged instructions**: Applications run strictly in Ring 3 with full MMU protection. Invalid memory accesses are safely trapped without kernel panics.
- **Window framing & compositing**: Title bars, active glow borders, drop shadows, window dragging, split-screen clipping, and VESA 800×600 upscaling are handled automatically by the monolithic kernel compositor.
- **Polling overhead**: `sleep(ms)` and `yield()` cooperate with the preemptive scheduler so background processes and the desktop interface continue running smoothly.

---

## 8. Built-in Utilities & Desktop Features (v0.6 Nexus)

ArchaOS v0.6 provides a suite of advanced native CLI tools and desktop enhancements:

### 8.1 Rich Markdown Viewer (`mdview`)
- **Syntax**: `mdview <filename.md>`
- **Capabilities**: Formats markdown headings (`#`, `##`, `###`), bold (`**bold**`), italic (`*italic*`), code fences (` ``` `), inline code (`` `code` ``), numbered and bulleted lists, and blockquotes with color-coded syntax.

### 8.2 Interactive Hex Editor (`hexedit`)
- **Syntax**: `hexedit <filename>`
- **Navigation & Controls**:
  - `Arrow Keys`: Move cursor across 16-byte hex matrix or ASCII column.
  - `Tab`: Toggle editing between Hex byte mode and ASCII character mode.
  - `0-9, a-f`: Edit hex nibbles directly in memory buffer.
  - `Ctrl+S` / `F2`: Save modifications back to file.
  - `Esc` / `Ctrl+Q`: Exit editor.

### 8.3 Terminal Multiplexer (`archmux`)
- **Syntax**: `archmux`
- **Features**:
  - Dual split-screen terminal layout with active pane border highlight.
  - `Tab` / `Ctrl+B`: Switch active pane focus.
  - Independent scrollback history and execution in each pane.
  - Built-in commands: `help`, `ls`, `mem`, `uptime`, `date`, `fortune`, `clear`, `exit`.

### 8.4 MicroPython CLI & REPL (`python`)
- **Syntax**:
  - Interactive REPL: `python` (enters `>>> ` prompt; type `exit()` or `quit()` to exit).
  - Inline code execution: `python -c "print(10 + 20 * 3)"`
  - Script file execution: `python script.py`

### 8.5 Desktop QuickRunner / Spotlight (`Alt+Space`)
- Press **`Alt + Space`** anywhere on the graphical desktop to open the floating Spotlight bar.
- **Live Math Evaluation**: Type any arithmetic expression (e.g. `24 * 60` or `1024 * 768 / 2`) for instant live calculation without pressing Enter.
- **Fuzzy App Launching**: Type the name of any app (e.g. `calc`, `coreview`, `paint`, `doom`) and hit `Enter` to launch immediately. Press `Esc` to close.

### 8.6 Dual Virtual Workspaces
- **Taskbar Pager**: Click `[ 1 ]` or `[ 2 ]` in the taskbar next to the clock.
- **Hotkeys**: Press `Ctrl+1` or `Ctrl+2` to switch directly, or `Ctrl+Tab` to cycle between workspaces.
- **Window Isolation**: Windows opened on Workspace 1 stay organized on Workspace 1, giving you a clutter-free dual desktop.

### 8.7 Enhanced Painter (9 Drawing Tools)
- Open `Painter` from the Start Menu or desktop.
- 9 Geometric & Vector Tools:
  - `Pn`: Pencil (freehand single-pixel)
  - `Bs`: Brush (3x3 soft square)
  - `Er`: Eraser (clears to white background)
  - `Fl`: Flood Fill (connected-component bucket fill)
  - `Ln`: Line (Bresenham line tool with preview)
  - `Rc`: Rect (unfilled rectangle outline)
  - `Bx`: Box (solid filled rectangle)
  - `Cr`: Circle (midpoint circle algorithm)
  - `Ey`: Eyedropper (picks color directly from canvas)

### 8.8 Iconic Screensavers with Game-Suppression
- Automatic idle activation with 3 retro screensaver modes:
  1. **3D Warp Starfield**: 128 pseudo-3D stars with high-speed depth projection.
  2. **3D Rotating Wireframe Cube**: Real-time perspective projection with fixed-point trigonometric rotation.
  3. **Mystify Curves**: Bouncing polygon vertices with rainbow trail history.
- **Manual Cycling**: Press `Space` or `Tab` while a screensaver is active to switch modes.
- **Smart Game-Suppression**: When a game (like DOOM or ScummVM) or active userland window is open, the screensaver timer is automatically suppressed so your gameplay is never interrupted.


