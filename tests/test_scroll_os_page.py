import subprocess
import time
import os
import socket

SERIAL_LOG = "/tmp/archaos_test/os_page_serial.log"
os.makedirs("/tmp/archaos_test", exist_ok=True)
if os.path.exists(SERIAL_LOG): os.remove(SERIAL_LOG)

QEMU_CMD = [
    "qemu-system-i386",
    "-cdrom", "ArchaOS.iso",
    "-m", "256M",
    "-netdev", "user,id=n0,hostname=archaos",
    "-device", "e1000,netdev=n0",
    "-serial", f"file:{SERIAL_LOG}",
    "-monitor", "telnet:127.0.0.1:4498,server,nowait",
    "-display", "none",
    "-boot", "d"
]

proc = subprocess.Popen(QEMU_CMD)

try:
    for _ in range(40):
        time.sleep(0.5)
        if os.path.exists(SERIAL_LOG):
            with open(SERIAL_LOG, "r") as f:
                if "Entering shell" in f.read(): break

    time.sleep(2.0)
    mon = socket.socket()
    mon.connect(("127.0.0.1", 4498))
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
    send("sendkey g", 0.1)
    send("sendkey u", 0.1)
    send("sendkey i", 0.1)
    send("sendkey ret", 3.5)

    # Click [Wikipedia]
    send("mouse_move -10 -42", 0.3)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.3)
    time.sleep(12.0)

    # Click search input
    send("mouse_move -80 76", 0.3)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.3)
    time.sleep(0.8)

    # Type query
    for c in "operating system":
        if c == " ": send("sendkey spc", 0.08)
        else: send(f"sendkey {c}", 0.08)
    time.sleep(0.5)

    # Enter
    send("sendkey ret", 0.2)
    time.sleep(13.0)

    # Click in viewport to focus page scrolling
    send("mouse_move 0 20", 0.2)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.2)

    # Scroll down 4 times
    for _ in range(4):
        send("sendkey pgdn", 0.4)
    time.sleep(1.0)
    cap("verify_operating_system_article_scrolled")

    mon.close()

finally:
    proc.terminate()
    proc.wait()

print("Done!")
