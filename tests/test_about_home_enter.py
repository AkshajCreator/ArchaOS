import subprocess, time, os, socket

SERIAL_LOG = "/tmp/archaos_test/about_home_test.log"
os.makedirs("/tmp/archaos_test", exist_ok=True)
if os.path.exists(SERIAL_LOG): os.remove(SERIAL_LOG)

QEMU_CMD = [
    "qemu-system-i386",
    "-cdrom", "ArchaOS.iso",
    "-m", "256M",
    "-netdev", "user,id=n0,hostname=archaos",
    "-device", "e1000,netdev=n0",
    "-serial", f"file:{SERIAL_LOG}",
    "-monitor", "telnet:127.0.0.1:4491,server,nowait",
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
    mon.connect(("127.0.0.1", 4491))
    time.sleep(0.2)
    def send(cmd, delay=0.1):
        mon.sendall((cmd + "\n").encode())
        time.sleep(delay)

    send("sendkey g", 0.1)
    send("sendkey u", 0.1)
    send("sendkey i", 0.1)
    send("sendkey ret", 3.5)

    # Focus address bar (click it at x=100, y=28)
    # Move mouse from (160, 100) to (100, 28): dx=-60, dy=-72
    send("mouse_move -60 -72", 0.2)
    send("mouse_button 1", 0.1)
    send("mouse_button 0", 0.2)

    # Press Enter in address bar while it has about:home
    send("sendkey ret", 2.0)

    # Dump screen
    send("screendump /tmp/archaos_test/about_home_enter.ppm", 0.4)
    subprocess.run(["ffmpeg", "-y", "-i", "/tmp/archaos_test/about_home_enter.ppm",
                    "/home/akushaji/.gemini/antigravity/brain/7c1657c8-11d2-45ed-89b6-04b6729cbc04/images/test_about_home_enter.png"], capture_output=True)

    with open(SERIAL_LOG) as f:
        print("Serial log tail:")
        print("".join(f.readlines()[-30:]))
    mon.close()
finally:
    proc.terminate()
    proc.wait()
