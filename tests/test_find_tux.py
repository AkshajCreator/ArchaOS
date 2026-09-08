import subprocess
import time
import os
import socket

SERIAL_LOG = "/tmp/archaos_test/find_tux_serial.log"
os.makedirs("/tmp/archaos_test", exist_ok=True)
if os.path.exists(SERIAL_LOG): os.remove(SERIAL_LOG)

QEMU_CMD = [
    "qemu-system-i386",
    "-cdrom", "ArchaOS.iso",
    "-m", "256M",
    "-netdev", "user,id=n0,hostname=archaos",
    "-device", "e1000,netdev=n0",
    "-serial", f"file:{SERIAL_LOG}",
    "-monitor", "telnet:127.0.0.1:4494,server,nowait",
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
    mon.connect(("127.0.0.1", 4494))
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

    send("sendkey g", 0.1)
    send("sendkey u", 0.1)
    send("sendkey i", 0.1)
    send("sendkey ret", 3.5)

    type_url("https://en.wikipedia.org/wiki/Linux")
    time.sleep(13.0)

    # Click in viewport to focus page scrolling
    send("mouse_move 0 20", 0.2)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.2)

    # Scroll down 2 times
    send("sendkey pgdn", 0.4)
    send("sendkey pgdn", 0.4)
    time.sleep(0.5)
    cap("verify_tux_scrolled_2")

    send("sendkey pgdn", 0.4)
    time.sleep(0.5)
    cap("verify_tux_scrolled_3")

    mon.close()

finally:
    proc.terminate()
    proc.wait()

print("Done!")
