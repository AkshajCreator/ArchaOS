import subprocess
import time
import os
import socket
from PIL import Image

ARTIFACTS_DIR = "/home/akushaji/.gemini/antigravity/brain/7c1657c8-11d2-45ed-89b6-04b6729cbc04/images"
os.makedirs(ARTIFACTS_DIR, exist_ok=True)
os.makedirs("/tmp/archaos_test", exist_ok=True)
serial_log = "/tmp/archaos_test/final_media_serial.log"

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
    "-monitor", "telnet:127.0.0.1:4447,server,nowait",
    "-display", "none",
    "-boot", "d"
]

print("Launching QEMU for final verification...")
proc = subprocess.Popen(qemu_cmd)

def send_monitor(cmd):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", 4447))
        time.sleep(0.04)
        s.sendall((cmd + "\n").encode())
        time.sleep(0.08)
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

    # 1. CLI: audio /audio/linus.ogg
    type_keys("audio /audio/linus.ogg")
    send_monitor("sendkey ret")
    time.sleep(1.5)
    capture_screen("final_01_cli_audio_ogg")

    # 2. CLI: audio status
    type_keys("audio status")
    send_monitor("sendkey ret")
    time.sleep(1.0)
    capture_screen("final_02_cli_audio_status")

    # 3. Enter GUI
    type_keys("gui")
    send_monitor("sendkey ret")
    time.sleep(4.0)

    # 4. Open Audio Player via Start Menu (item 12 at y = 151)
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 16 193")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(0.8)

    send_monitor("mouse_move 20 -42")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(2.0)
    capture_screen("final_03_audioplayer_opened")

    # Let Audio Player play with animated spectrum
    time.sleep(2.0)
    capture_screen("final_04_audioplayer_playing")

    # Click Quick Media Link [Theme WAV] (Button at x=88, y=135)
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 88 135")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(2.5)
    capture_screen("final_05_audioplayer_theme_wav")

    # Close/minimize Audio Player (click close button at x = 16 + 205 - 7 = 214, y = 16 + 6 = 22)
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 214 22")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(0.5)

    # 5. Open Video Player via Start Menu (item 13 at y = 163)
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 16 193")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(0.8)

    send_monitor("mouse_move 20 -30")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(2.0)
    capture_screen("final_06_videoplayer_opened")

    # Click [Play] in Video Player (button at x = 26 + 20 = 46, y = 14 + 135 = 149)
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 46 149")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(2.0)
    capture_screen("final_07_videoplayer_playing")

    # Toggle CRT filter (button at x = 26 + 135 = 161, y = 149)
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 161 149")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(1.5)
    capture_screen("final_08_videoplayer_crt")

    # Close Video Player
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 243 20")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(0.5)

    # 6. Open Web Browser and test audio link interception
    # Start Menu -> Web Browser (item 11 at y = 139)
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 16 193")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(0.8)

    send_monitor("mouse_move 20 -54")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(2.0)
    capture_screen("final_09_browser_opened")

    # Click in browser address bar (x = 100, y = 30)
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 100 30")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(0.2)

    # Clear address bar
    for _ in range(25):
        send_monitor("sendkey backspace")
        time.sleep(0.04)

    # Type /wiki/File:Linus-linux.ogg (the exact Wikipedia pronunciation file target)
    type_keys("/wiki/File:Linus-linux.ogg")
    send_monitor("sendkey ret")
    time.sleep(3.0)
    capture_screen("final_10_browser_audio_intercepted")

    # Read serial log
    if os.path.exists(serial_log):
        with open(serial_log, "r", errors="ignore") as f:
            print("\n=== FINAL SERIAL LOG EXCERPT ===")
            lines = f.readlines()
            for line in lines[-50:]:
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
