import subprocess
import time
import os
import socket
from PIL import Image

ARTIFACTS_DIR = "/home/akushaji/.gemini/antigravity/brain/7c1657c8-11d2-45ed-89b6-04b6729cbc04/images"
os.makedirs(ARTIFACTS_DIR, exist_ok=True)
os.makedirs("/tmp/archaos_test", exist_ok=True)
serial_log = "/tmp/archaos_test/universal_media_serial.log"

if os.path.exists(serial_log):
    os.remove(serial_log)

qemu_cmd = [
    "qemu-system-i386",
    "-cdrom", "ArchaOS.iso",
    "-m", "256M",
    "-audiodev", "none,id=snd0",
    "-device", "sb16,audiodev=snd0",
    "-netdev", "user,id=n0,hostname=archaos",
    "-device", "e1000,netdev=n0",
    "-serial", f"file:{serial_log}",
    "-monitor", "telnet:127.0.0.1:4446,server,nowait",
    "-display", "none",
    "-boot", "d"
]

print("Launching QEMU...")
proc = subprocess.Popen(qemu_cmd)

def send_monitor(cmd):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", 4446))
        time.sleep(0.05)
        s.sendall((cmd + "\n").encode())
        time.sleep(0.1)
        s.close()
    except Exception as e:
        print("Monitor error:", e)

def type_keys(s):
    for c in s:
        if c == ' ':
            send_monitor("sendkey spc")
        elif c == '/':
            send_monitor("sendkey slash")
        elif c == '.':
            send_monitor("sendkey dot")
        elif c == '-':
            send_monitor("sendkey minus")
        elif c == '_':
            send_monitor("sendkey shift-minus")
        elif c == ':':
            send_monitor("sendkey shift-semicolon")
        else:
            send_monitor(f"sendkey {c}")
        time.sleep(0.06)

def capture_screen(name):
    ppm_path = f"/tmp/archaos_test/{name}.ppm"
    png_path = os.path.join(ARTIFACTS_DIR, f"{name}.png")
    send_monitor(f"screendump {ppm_path}")
    time.sleep(0.4)
    if os.path.exists(ppm_path):
        img = Image.open(ppm_path)
        img.save(png_path)
        print(f"Captured {png_path} ({img.size})")
        return png_path
    return None

try:
    # Wait for boot
    print("Waiting for boot...")
    for _ in range(30):
        time.sleep(0.5)
        if os.path.exists(serial_log):
            with open(serial_log, "r", errors="ignore") as f:
                content = f.read()
                if "Entering shell" in content or "ArchaOS Booted Successfully" in content:
                    print("Shell detected!")
                    break
    time.sleep(4.0)

    # 1. Test CLI audio command with OGG Vorbis
    print("Testing CLI: audio /audio/linus.ogg")
    type_keys("audio /audio/linus.ogg")
    send_monitor("sendkey ret")
    time.sleep(2.0)
    capture_screen("01_cli_audio_ogg")

    # 2. Test CLI audio status
    print("Testing CLI: audio status")
    type_keys("audio status")
    send_monitor("sendkey ret")
    time.sleep(1.5)
    capture_screen("02_cli_audio_status")

    # 3. Enter GUI
    print("Entering GUI...")
    type_keys("gui")
    send_monitor("sendkey ret")
    time.sleep(4.0)
    capture_screen("03_gui_desktop")

    # 4. Open Audio Player via Start Menu (item 12)
    print("Opening Audio Player via Start Menu...")
    send_monitor("mouse_move -320 -200") # move to (0,0)
    time.sleep(0.2)
    send_monitor("mouse_move 16 193")   # Start button
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(0.8)

    # Item 12 (Audio Player) at y = 151 -> delta from 193 is -42
    send_monitor("mouse_move 20 -42")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(2.0)
    capture_screen("04_gui_audioplayer_opened")

    # Let Audio Player animate spectrum & oscilloscope
    time.sleep(2.0)
    capture_screen("05_gui_audioplayer_playing")

    # 5. Click Quick Media Link [Theme WAV]
    # Window 13 is at x=16, y=16
    # Button 2 [Theme WAV] is at x = 16 + 70 = 86, y = 16 + 119 = 135
    print("Clicking Quick Media Link [Theme WAV]...")
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 88 135")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(2.5)
    capture_screen("06_gui_audioplayer_theme_wav")

    # 6. Open Video Player via Start Menu (item 13)
    print("Opening Video Player via Start Menu...")
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 16 193")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(0.8)

    # Item 13 (Video Player) at y = 163 -> delta from 193 is -30
    send_monitor("mouse_move 20 -30")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(2.5)
    capture_screen("07_gui_videoplayer_opened")

    # Click [ > Play ] in Video Player (button at w->x(26) + 4 = 30, by = 14 + 135 = 149)
    print("Clicking Video Player [Play]...")
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 35 149")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(2.5)
    capture_screen("08_gui_videoplayer_playing")

    # Toggle CRT filter in Video Player (button at w->x(26) + 105 = 131, by = 149)
    print("Toggling CRT filter...")
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 135 149")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(1.5)
    capture_screen("09_gui_videoplayer_crt")

    # 7. Test Browser Media Link Interception
    # Bring Browser (window 12) to focus
    print("Focusing Web Browser...")
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    # Click browser titlebar or address bar (x = 100, y = 30)
    send_monitor("mouse_move 100 30")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(0.5)

    # Click in address bar
    send_monitor("mouse_move 80 32")
    time.sleep(0.1)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(0.2)

    # Clear address bar
    for _ in range(25):
        send_monitor("sendkey backspace")
        time.sleep(0.04)

    # Type Wikipedia Linus pronunciation media file link!
    type_keys("https://upload.wikimedia.org/wikipedia/commons/0/03/Linus-linux.ogg")
    send_monitor("sendkey ret")
    time.sleep(3.0)
    capture_screen("10_gui_browser_audio_intercepted")

    # Check serial log
    if os.path.exists(serial_log):
        with open(serial_log, "r", errors="ignore") as f:
            print("\n=== SERIAL LOG EXCERPT ===")
            lines = f.readlines()
            for line in lines[-40:]:
                print(line.strip())

finally:
    send_monitor("quit")
    time.sleep(0.5)
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except:
        proc.kill()
    print("QEMU stopped.")
