import subprocess
import time
import os
import socket

SERIAL_LOG = "/tmp/archaos_test/ondemand_serial.log"
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
    print("Waiting for boot...")
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
        ppm = f"/tmp/archaos_test/{name}.ppm"
        if os.path.exists(ppm): os.remove(ppm)
        send(f"screendump {ppm}", 0.4)
        out_path = f"/home/akushaji/.gemini/antigravity/brain/7c1657c8-11d2-45ed-89b6-04b6729cbc04/images/{name}.png"
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        if os.path.exists(ppm):
            subprocess.run(["ffmpeg", "-y", "-i", ppm, out_path], capture_output=True)
            print(f"Captured: {out_path}")

    # Launch GUI
    send("sendkey g", 0.1)
    send("sendkey u", 0.1)
    send("sendkey i", 0.1)
    send("sendkey ret", 3.5)

    print("Browser started. Clicking [Media] bookmark...")
    # Bookmarks bar [Media] is at x=220, y=44 (window is at x=0, y=0)
    # Move mouse to (220, 44) from (160, 100): dx = 60, dy = -56
    send("mouse_move 60 -56", 0.2)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.5)

    time.sleep(1.0)
    cap("verify_ondemand_1_unloaded")

    # In about:media, Tux card is in the first card below navbar
    # Navbar is around y=52..68, Card 1 starts at y=70, image card is around x=10..180, y=80..120
    # Let's move cursor over the image card and click it!
    # Move from (220, 44) to (60, 105): dx = -160, dy = 61
    print("Clicking image card to load on-demand...")
    send("mouse_move -160 61", 0.2)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.5)

    # Wait for image fetch & decode
    print("Waiting for fetch and decode...")
    for _ in range(15):
        time.sleep(1.0)
        if os.path.exists(SERIAL_LOG):
            with open(SERIAL_LOG, "r") as f:
                content = f.read()
                if "250x297" in content or "Image loaded" in content:
                    print("Found Image loaded in log!")
                    break

    time.sleep(2.0)
    cap("verify_ondemand_2_loaded")

    if os.path.exists(SERIAL_LOG):
        with open(SERIAL_LOG, "r") as f:
            print("Serial log tail:")
            lines = f.readlines()
            for l in lines[-35:]:
                print("  ", l.strip())

    mon.close()

finally:
    proc.terminate()
    proc.wait()

print("Done!")
