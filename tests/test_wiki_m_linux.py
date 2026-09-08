import subprocess
import time
import os
import socket

SERIAL_LOG = "/tmp/archaos_test/wiki_m_serial.log"
os.makedirs("/tmp/archaos_test", exist_ok=True)
if os.path.exists(SERIAL_LOG): os.remove(SERIAL_LOG)

QEMU_CMD = [
    "qemu-system-i386",
    "-cdrom", "ArchaOS.iso",
    "-m", "256M",
    "-netdev", "user,id=n0,hostname=archaos",
    "-device", "e1000,netdev=n0",
    "-serial", f"file:{SERIAL_LOG}",
    "-monitor", "telnet:127.0.0.1:4492,server,nowait",
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
    mon.connect(("127.0.0.1", 4492))
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

    def type_url(url):
        send("sendkey f6", 0.3)
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
        send("sendkey ret", 0.3)

    # Launch GUI
    send("sendkey g", 0.1)
    send("sendkey u", 0.1)
    send("sendkey i", 0.1)
    send("sendkey ret", 3.5)

    print("Navigating to https://en.m.wikipedia.org/wiki/Linux ...")
    type_url("https://en.m.wikipedia.org/wiki/Linux")

    # Wait for page download AND DOM parse
    print("Waiting for page load and layout...")
    for i in range(60):
        time.sleep(1.0)
        if os.path.exists(SERIAL_LOG):
            with open(SERIAL_LOG, "r") as f:
                content = f.read()
                if "[HTTP] total=" in content:
                    part = content.split("[HTTP] total=")[-1]
                    if "[NetSurf] Full DOM Layout generated" in part:
                        print(f"Page layout generated after {i}s!")
                        break

    time.sleep(2.0)
    cap("verify_wiki_m_initial")

    # Focus viewport and scroll down to find Tux
    send("mouse_move 0 20", 0.2)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.2)

    for step in range(1, 6):
        send("sendkey pgdn", 0.4)
        time.sleep(0.4)
        cap(f"verify_wiki_m_pgdn{step}")

    if os.path.exists(SERIAL_LOG):
        with open(SERIAL_LOG, "r") as f:
            print("Serial log tail:")
            lines = f.readlines()
            for l in lines[-30:]:
                print("  ", l.strip())

    mon.close()

finally:
    proc.terminate()
    proc.wait()

print("Done!")
