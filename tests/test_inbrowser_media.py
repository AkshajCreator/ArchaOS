import subprocess
import time
import os
import socket
from PIL import Image

ARTIFACTS_DIR = "/home/akushaji/.gemini/antigravity/brain/7c1657c8-11d2-45ed-89b6-04b6729cbc04/images"
os.makedirs(ARTIFACTS_DIR, exist_ok=True)
os.makedirs("/tmp/archaos_test", exist_ok=True)
serial_log = "/tmp/archaos_test/inbrowser_serial.log"

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
    "-monitor", "telnet:127.0.0.1:4451,server,nowait",
    "-display", "none",
    "-boot", "d"
]

print("Launching QEMU...")
proc = subprocess.Popen(qemu_cmd)

def send_monitor(cmd):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", 4451))
        time.sleep(0.04)
        s.sendall((cmd + "\n").encode())
        time.sleep(0.06)
        s.close()
    except Exception as e:
        print("Monitor error:", e)

cur_x = 160
cur_y = 100

def move_to(target_x, target_y):
    global cur_x, cur_y
    dx = target_x - cur_x
    dy = target_y - cur_y
    while dx != 0 or dy != 0:
        step_x = max(-30, min(30, dx))
        step_y = max(-30, min(30, dy))
        send_monitor(f"mouse_move {step_x} {step_y}")
        dx -= step_x
        dy -= step_y
        time.sleep(0.03)
    cur_x = target_x
    cur_y = target_y
    time.sleep(0.1)

def click_at(x, y):
    move_to(x, y)
    send_monitor("mouse_button 1")
    time.sleep(0.08)
    send_monitor("mouse_button 0")
    time.sleep(0.2)

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
        time.sleep(0.08)

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
    capture_screen("inbrowser_01_gui_ready")

    # Click [Media] bookmark at (210, 58)
    print("Clicking Media bookmark at (210, 58)...")
    click_at(210, 58)
    time.sleep(2.5)
    capture_screen("inbrowser_02_about_media")

    # Click in-page <video> element at (146, 119)
    print("Clicking in-page <video> element at (146, 119)...")
    click_at(146, 119)
    time.sleep(2.0)
    capture_screen("inbrowser_03_video_playing")

    # Click [Linus .ogg] link in navbar (around x=115, y=75) to play Vorbis audio!
    print("Clicking [Linus .ogg] link at (115, 75)...")
    click_at(115, 75)
    time.sleep(2.5)
    capture_screen("inbrowser_04_audio_playing")

    # Direct URL navigation:
    # Click Address bar at (140, 46) to focus and clear it
    print("Clicking address bar at (140, 46)...")
    click_at(140, 46)
    time.sleep(0.3)

    # Type /media/demo.vid and hit Enter
    print("Entering /media/demo.vid in address bar...")
    type_keys("/media/demo.vid")
    send_monitor("sendkey ret")
    time.sleep(3.0)
    capture_screen("inbrowser_05_direct_media_url")

    # Check serial log
    if os.path.exists(serial_log):
        with open(serial_log, "r", errors="ignore") as f:
            print("\n=== SERIAL LOG EXCERPT ===")
            for line in f:
                if "[BROWSER_" in line or "[NS_" in line or "[Video]" in line or "[MediaFetch]" in line:
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
