import subprocess
import time
import os
import socket

SERIAL_LOG = "/tmp/archaos_test/verify_search_serial.log"
os.makedirs("/tmp/archaos_test", exist_ok=True)
if os.path.exists(SERIAL_LOG): os.remove(SERIAL_LOG)

QEMU_CMD = [
    "qemu-system-i386",
    "-cdrom", "ArchaOS.iso",
    "-m", "256M",
    "-netdev", "user,id=n0,hostname=archaos",
    "-device", "e1000,netdev=n0",
    "-serial", f"file:{SERIAL_LOG}",
    "-monitor", "telnet:127.0.0.1:4497,server,nowait",
    "-display", "none",
    "-boot", "d"
]

print("Starting QEMU...")
proc = subprocess.Popen(QEMU_CMD)

try:
    print("Waiting for shell...")
    for _ in range(40):
        time.sleep(0.5)
        if os.path.exists(SERIAL_LOG):
            with open(SERIAL_LOG, "r") as f:
                if "Entering shell" in f.read(): break

    time.sleep(2.0)
    mon = socket.socket()
    mon.connect(("127.0.0.1", 4497))
    time.sleep(0.2)

    def send(cmd, delay=0.1):
        mon.sendall((cmd + "\n").encode())
        time.sleep(delay)

    def cap(name):
        ppm = "/tmp/archaos_test/temp_screen.ppm"
        if os.path.exists(ppm): os.remove(ppm)
        send(f"screendump {ppm}", 0.4)
        out_path = f"/home/akushaji/.gemini/antigravity/brain/7c1657c8-11d2-45ed-89b6-04b6729cbc04/images/{name}.png"
        if os.path.exists(ppm):
            subprocess.run(["ffmpeg", "-y", "-i", ppm, out_path], capture_output=True)
            print(f"Captured: {name}.png")

    # Launch GUI
    print("Launching GUI...")
    send("sendkey g", 0.1)
    send("sendkey u", 0.1)
    send("sendkey i", 0.1)
    send("sendkey ret", 3.5)

    print("1. Clicking [Wikipedia] Bookmark button...")
    # Mouse starts at (160, 100). [Wikipedia] is at (150, 58). dx = -10, dy = -42
    send("mouse_move -10 -42", 0.3)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.3)
    print("Waiting 12 seconds for Wikipedia to load...")
    time.sleep(12.0)
    cap("step1_wiki_main")

    print("2. Clicking inside Search Input Box at (70, 134)...")
    # Current mouse is at (150, 58). Search input is at (70, 134). dx = -80, dy = 76
    send("mouse_move -80 76", 0.3)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.3)
    time.sleep(0.8)
    cap("step2_search_focused")

    print("3. Typing 'operating system' into search box...")
    for c in "operating system":
        if c == " ": send("sendkey spc", 0.08)
        else: send(f"sendkey {c}", 0.08)
    time.sleep(0.5)
    cap("step3_search_typed")

    print("4. Pressing Enter to submit search...")
    send("sendkey ret", 0.2)
    print("Waiting 14 seconds for search results...")
    time.sleep(14.0)
    cap("step4_search_submitted")

    mon.close()

finally:
    print("Stopping QEMU...")
    proc.terminate()
    proc.wait()

print("Verification complete!")
