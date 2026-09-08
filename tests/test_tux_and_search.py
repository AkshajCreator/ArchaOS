import subprocess
import time
import os
import socket

SERIAL_LOG = "/tmp/archaos_test/tux_serial.log"
os.makedirs("/tmp/archaos_test", exist_ok=True)
if os.path.exists(SERIAL_LOG): os.remove(SERIAL_LOG)

QEMU_CMD = [
    "qemu-system-i386",
    "-cdrom", "ArchaOS.iso",
    "-m", "256M",
    "-netdev", "user,id=n0,hostname=archaos",
    "-device", "e1000,netdev=n0",
    "-serial", f"file:{SERIAL_LOG}",
    "-monitor", "telnet:127.0.0.1:4499,server,nowait",
    "-display", "none",
    "-boot", "d"
]

print("Starting QEMU...")
proc = subprocess.Popen(QEMU_CMD)

try:
    for _ in range(40):
        time.sleep(0.5)
        if os.path.exists(SERIAL_LOG):
            with open(SERIAL_LOG, "r") as f:
                if "Entering shell" in f.read(): break

    time.sleep(2.0)
    mon = socket.socket()
    mon.connect(("127.0.0.1", 4499))
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

    # 1. Test Linux page on Wikipedia
    print("Navigating to https://en.wikipedia.org/wiki/Linux...")
    type_url("https://en.wikipedia.org/wiki/Linux")
    print("Waiting 13 seconds for download...")
    time.sleep(13.0)

    # Scroll down into the infobox where Tux is located
    send("mouse_move 0 20", 0.2)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.2)
    # The user screenshot showed Linux title and Tux immediately below it
    cap("verify_linux_tux_mascot")

    # Scroll down a bit more
    send("sendkey pgdn", 0.4)
    time.sleep(1.0)
    cap("verify_linux_scrolled")

    # 2. Test DuckDuckGo Lite search (no bot verification captcha)
    print("Navigating to DuckDuckGo search...")
    # Click [DuckDuckGo] bookmark at (-80, -42) from center or type_url
    type_url("https://lite.duckduckgo.com/lite/?q=linux")
    print("Waiting 13 seconds for DuckDuckGo download...")
    time.sleep(13.0)
    cap("verify_ddg_no_captcha")

    mon.close()

finally:
    print("Stopping QEMU...")
    proc.terminate()
    proc.wait()

print("Verification run finished!")
