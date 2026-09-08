import subprocess
import time
import os
import socket

SERIAL_LOG = "/tmp/archaos_test/search_click_serial.log"
os.makedirs("/tmp/archaos_test", exist_ok=True)
if os.path.exists(SERIAL_LOG): os.remove(SERIAL_LOG)

QEMU_CMD = [
    "qemu-system-i386",
    "-cdrom", "ArchaOS.iso",
    "-m", "256M",
    "-netdev", "user,id=n0,hostname=archaos",
    "-device", "e1000,netdev=n0",
    "-serial", f"file:{SERIAL_LOG}",
    "-monitor", "telnet:127.0.0.1:4495,server,nowait",
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
    mon.connect(("127.0.0.1", 4495))
    time.sleep(0.2)

    def send(cmd, delay=0.08):
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

    def type_url(url):
        send("sendkey f6", 0.2)
        for c in url:
            if c == ":": send("sendkey shift-semicolon")
            elif c == "/": send("sendkey slash")
            elif c == ".": send("sendkey dot")
            elif c == "_": send("sendkey shift-minus")
            elif c == "-": send("sendkey minus")
            elif c == "?": send("sendkey shift-slash")
            elif c == "=": send("sendkey equal")
            elif c.isupper(): send(f"sendkey shift-{c.lower()}")
            else: send(f"sendkey {c}")
        send("sendkey ret", 0.2)

    # Launch GUI
    print("Launching GUI...")
    send("sendkey g", 0.1)
    send("sendkey u", 0.1)
    send("sendkey i", 0.1)
    send("sendkey ret", 3.5)

    print("1. Navigating to Wikipedia Main Page...")
    type_url("https://en.m.wikipedia.org/wiki/Main_Page")
    print("Waiting 12 seconds for Wikipedia download...")
    time.sleep(12.0)
    cap("verify_wiki_before_click")

    print("2. Clicking inside Search Input Box...")
    # The search input is located around x=70, y=95 on screen (window is at x=4, y=14; content view_y=64, input is at y=25 in page -> screen y=89)
    # Let's click at x=70, y=89
    # Mouse move in QEMU monitor: move from center (160, 100) by dx = 70 - 160 = -90, dy = 89 - 100 = -11
    send("mouse_move -90 -11", 0.3)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.3)
    time.sleep(1.0)
    cap("verify_wiki_after_search_click")

    print("3. Typing 'operating system' into search box...")
    for c in "operating system":
        if c == " ": send("sendkey spc", 0.05)
        else: send(f"sendkey {c}", 0.05)
    time.sleep(0.5)
    cap("verify_wiki_search_typed")

    print("4. Navigating to https://en.wikipedia.org/wiki/About to verify table notice box...")
    type_url("https://en.wikipedia.org/wiki/About")
    print("Waiting 12 seconds for About download...")
    time.sleep(12.0)
    cap("verify_about_page_no_overlap")

    mon.close()

finally:
    print("Stopping QEMU...")
    proc.terminate()
    proc.wait()

print("Verification complete!")
