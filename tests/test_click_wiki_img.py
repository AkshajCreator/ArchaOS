import subprocess, time, os, socket

SERIAL_LOG = "/tmp/archaos_test/wiki_img_click.log"
os.makedirs("/tmp/archaos_test", exist_ok=True)
if os.path.exists(SERIAL_LOG): os.remove(SERIAL_LOG)

QEMU_CMD = [
    "qemu-system-i386",
    "-cdrom", "ArchaOS.iso",
    "-m", "256M",
    "-netdev", "user,id=n0,hostname=archaos",
    "-device", "e1000,netdev=n0",
    "-serial", f"file:{SERIAL_LOG}",
    "-monitor", "telnet:127.0.0.1:4496,server,nowait",
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
    mon.connect(("127.0.0.1", 4496))
    time.sleep(0.2)
    def send(cmd, delay=0.1):
        mon.sendall((cmd + "\n").encode())
        time.sleep(delay)

    def cap(name):
        ppm = f"/tmp/archaos_test/{name}.ppm"
        if os.path.exists(ppm): os.remove(ppm)
        send(f"screendump {ppm}", 0.4)
        out_png = f"/home/akushaji/.gemini/antigravity/brain/7c1657c8-11d2-45ed-89b6-04b6729cbc04/images/{name}.png"
        os.makedirs(os.path.dirname(out_png), exist_ok=True)
        if os.path.exists(ppm):
            subprocess.run(["ffmpeg", "-y", "-i", ppm, out_png], capture_output=True)
            print(f"Captured: {out_png}")

    # Launch GUI
    send("sendkey g", 0.1)
    send("sendkey u", 0.1)
    send("sendkey i", 0.1)
    send("sendkey ret", 3.5)

    # Click Wikipedia bookmark: (150, 58)
    # Initial mouse at (160, 100): dx = 150 - 160 = -10, dy = 58 - 100 = -42.
    print("Clicking Wikipedia bookmark...")
    send("mouse_move -10 -42", 0.2)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.5)

    # Wait for Wikipedia load
    for i in range(60):
        time.sleep(1.0)
        if os.path.exists(SERIAL_LOG):
            with open(SERIAL_LOG, "r") as f:
                content = f.read()
                if "Full DOM Layout generated" in content.split("en.wikipedia.org")[-1]:
                    print("Wikipedia layout ready!")
                    break

    time.sleep(1.0)
    cap("wiki_before_img_click")

    # Current mouse is at (150, 58).
    # Click the [IMG] / [Click] card at (30, 125):
    # dx = 30 - 150 = -120, dy = 125 - 58 = 67.
    print("Clicking [IMG] card at (30, 125)...")
    send("mouse_move -120 67", 0.2)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 4.0)

    cap("wiki_after_img_click")

    with open(SERIAL_LOG) as f:
        print("=== SERIAL LOG [ImageFetch] & [HTTP] lines ===")
        for line in f.readlines():
            if any(k in line for k in ["[ImageFetch]", "[HTTP]", "[DNS]", "[TLS]", "Failed to load image"]):
                print(line.strip())

    mon.close()
finally:
    proc.terminate()
    proc.wait()
