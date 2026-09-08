# CLI Shell and Terminal Engine

The ArchaOS interactive command-line interface (`Arc/>`) provides a powerful Unix-inspired terminal environment running in 80×25 text mode with hardware video memory at `0xB8000`.

---

## ⚡ Next-Generation Shell Features

### 1. Fish-Style Inline Autosuggestions
As you type at the `Arc/>` prompt, ArchaOS compares your active prefix against command history and registered system binaries:
- The suggested completion is rendered directly ahead of the cursor in dim gray text (`0x08`).
- Press **Right Arrow** or **Tab** to immediately accept and auto-fill the suggestion.

### 2. Reverse Incremental History Search (`Ctrl+R`)
- Press **Ctrl+R** to enter interactive backwards history search mode.
- The prompt transitions to `(reverse-i-search)'<query>': <matched_command>`.
- Type characters to filter matches in real time.
- Press **Ctrl+R** again to step through earlier matches.
- Press **Enter** to immediately execute the matched command.
- Press **Tab** or **Right Arrow** to exit search mode and place the command on the editing line.
- Press **ESC** or **Ctrl+C** to cancel and return to a clean prompt.

### 3. Full In-Kernel ANSI Escape Sequence Engine
ArchaOS implements a state machine directly in the kernel to parse standard ECMA-48 / ANSI terminal escape sequences:
- **SGR Foreground Colors**: Codes 30–37 (standard) and 90–97 (bright/high-intensity) mapped to hardware VGA colors.
- **SGR Background Colors**: Codes 40–47 and 100–107.
- **Text Attributes**: Bold (`1`), Dim (`2`), Inverse Video (`7`), and Reset (`0`).
- **Cursor Positioning**: Up (`A`), Down (`B`), Forward (`C`), Backward (`D`), Position (`H`/`f`), Save Cursor (`s`), Restore Cursor (`u`).
- **Screen & Line Erasing**: Clear screen (`\e[2J`), Clear to end of line (`\e[K`).

### 4. Intelligent ANSI-Aware Word Wrap & Pagination
- **Word Wrap**: Shell output dynamically counts visible character widths (stripping out ANSI escape sequences) and breaks lines cleanly at whitespace boundaries instead of splitting words across columns.
- **Paginated Manuals**: Viewing large system files such as `/general` and `/new` launches the built-in pager:
  - `Enter`: Advance by one line.
  - `Space`: Advance by one full page.
  - `q`: Quit immediately back to the prompt.

---

## 📝 Authentic GNU nano 0.5.0 Micro-Editor

ArchaOS includes a full-screen text editor inspired by early GNU nano releases:
- Run `nano <filename>` or `edit <filename>`.
- Top Title Bar: Displays `GNU nano 0.5.0`, current filename, and `[Modified]` indicator when unsaved edits exist.
- 21-Line Text Canvas: Supports multi-line navigation, dynamic cursor rendering, and instant word wrapping.
- Status Feedback Row: Real-time cursor coordinates `[ Ln X, Col Y ]`.
- Two-Row Shortcut Legend:
  - `^O WriteOut`: Save file directly to the RAM virtual filesystem.
  - `^K Cut Text`: Delete current line and place into clipboard.
  - `^U Paste Text`: Insert clipboard line at cursor position.
  - `^X Exit` / `ESC`: Exit cleanly back to the `Arc/>` shell.

---

## 🔄 Universal Pipelines & Sequencing

### Universal Pipe Operator (`|`)
Connect the standard output of any command directly to the standard input of another:
```bash
help | grep Filesystem
fortune | wc
cat /general | less
```

### Semicolon Command Sequencing (`;`)
Execute multiple commands sequentially on a single line:
```bash
mkdir /test; touch /test/app.c; ls /test
```

### Environment Variables
ArchaOS provides an in-kernel environment variable table:
```bash
export USER=Akshaj
export EDITOR=nano
env
echo "Logged in as $USER using $EDITOR"
unset USER
```

---

## ⌨️ Complete Command Reference

| Category | Commands |
|---|---|
| **System** | `help`, `new`, `general`, `clear` / `cls`, `reboot`, `halt`, `uptime`, `date`, `top`, `neofetch`, `fortune`, `theme <name>`, `matrix`, `credits`, `meminfo`, `memtest` |
| **Filesystem** | `ls [path]`, `tree [path]`, `cd <path>`, `pwd`, `mkdir <path>`, `touch <file>`, `cat <file>`, `less <file>`, `nano <file>`, `head <file>`, `tail <file>`, `stat <file>`, `hexdump <file>`, `write <file> <text>`, `rm <file>`, `cp <src> <dst>`, `mv <src> <dst>`, `wc <file>`, `grep <pat> <file>`, `find` |
| **Networking** | `ifconfig`, `ping <host>`, `curl <url>`, `wget <url> [-O file]`, `ports` |
| **Shell & Env** | `export VAR=val`, `env`, `unset VAR`, `$VAR`, `cmd1; cmd2`, `cmd1 | cmd2`, `alias`, `unalias`, `run <script>`, `ansi` / `colors`, `echo [-e] <text>` |
| **Multimedia** | `audio [play|stop|pause|next|prev|list]`, `video`, `beep` |
| **Hardware** | `pci [list|scan]`, `serial [com1|com2] [write|read|status]`, `ata` |
| **Graphical** | `gui` (enters Mode 13h desktop environment) |
