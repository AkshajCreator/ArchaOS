import subprocess
import time
import os
import socket
from PIL import Image

ARTIFACTS_DIR = "/home/akushaji/.gemini/antigravity/brain/7c1657c8-11d2-45ed-89b6-04b6729cbc04/images"
os.makedirs(ARTIFACTS_DIR, exist_ok=True)
os.makedirs("/tmp/archaos_test", exist_ok=True)
serial_log = "/tmp/archaos_test/intercept_serial.log"

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

print("Launching QEMU...")
proc = subprocess.Popen(qemu_cmd)

def send_monitor(cmd):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", 4447))
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

    # Launch GUI
    print("Launching GUI...")
    type_keys("gui")
    send_monitor("sendkey ret")
    time.sleep(4.0)
    capture_screen("intercept_01_gui_ready")

    # In Browser (window 12 at x=16, y=10):
    # Bookmark bar: y in [49, 59]. Media is at x in [202, 248].
    print("Clicking Media bookmark at (215, 54)...")
    send_monitor("mouse_move -320 -200") # move to (0,0)
    time.sleep(0.2)
    send_monitor("mouse_move 215 54")
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(2.5)
    capture_screen("intercept_02_about_media")

    # In about:media:
    # Universal Audio Player link [Play Linus Torvalds Speech (.ogg)]
    # Or Universal Video Player link [Play ArchaOS 3D Wireframe (.vid)]
    # Let's click in the address bar cleanly:
    print("Clicking Browser address bar to test direct URL...")
    send_monitor("mouse_move -320 -200")
    time.sleep(0.2)
    send_monitor("mouse_move 120 43") # Address bar center is at y=43
    time.sleep(0.2)
    send_monitor("mouse_button 1")
    time.sleep(0.1)
    send_monitor("mouse_button 0")
    time.sleep(0.2)

    # Press Escape to clear the entire URL!
    print("Clearing URL bar via Escape...")
    send_monitor("sendkey esc")
    time.sleep(0.2)

    # Type /media/demo.vid and hit enter to test Video Player interception!
    print("Entering /media/demo.vid in address bar...")
    type_keys("/media/demo.vid")
    send_monitor("sendkey ret")
    time.sleep(2.5)
    capture_screen("intercept_03_video_player_opened")

    # Check serial log
    if os.path.exists(serial_log):
        with open(serial_log, "r", errors="ignore") as f:
            print("\n=== SERIAL LOG EXCERPT ===")
            lines = f.readlines()
            for line in lines[-30:]:
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
