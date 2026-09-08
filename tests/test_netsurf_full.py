import subprocess
import time
import os

os.makedirs("/tmp/archaos_test", exist_ok=True)
serial_log = "/tmp/archaos_test/serial.log"
png_home_page = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/netsurf_home_full.png"
png_frogfind = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/netsurf_frogfind_full.png"
png_video_stream = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/netsurf_video_player.png"

if os.path.exists(serial_log):
    os.remove(serial_log)

qemu_cmd = [
    "qemu-system-i386",
    "-cdrom", "ArchaOS.iso",
    "-m", "256M",
    "-netdev", "user,id=n0,hostname=archaos",
    "-device", "e1000,netdev=n0",
    "-serial", f"file:{serial_log}",
    "-monitor", "telnet:127.0.0.1:4444,server,nowait",
    "-display", "none",
    "-boot", "d"
]

proc = subprocess.Popen(qemu_cmd)

def send_monitor(cmd):
    try:
        import socket
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", 4444))
        time.sleep(0.1)
        s.sendall((cmd + "\n").encode())
        time.sleep(0.2)
        s.close()
    except Exception as e:
        print("Monitor error:", e)

for _ in range(40):
    time.sleep(0.5)
    if os.path.exists(serial_log):
        with open(serial_log, "r") as f:
            if "Entering shell" in f.read():
                break

time.sleep(4.0)

# Launch GUI
send_monitor("sendkey g")
time.sleep(0.25)
send_monitor("sendkey u")
time.sleep(0.25)
send_monitor("sendkey i")
time.sleep(0.25)
send_monitor("sendkey ret")
time.sleep(4.0)

# Open Web Browser via F3
send_monitor("sendkey f3")
time.sleep(3.0)

# 1. Capture Home Page
send_monitor("screendump /tmp/archaos_test/home.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/home.ppm"], stdout=open(png_home_page, "wb"))

# 2. Click [FrogFind] on Bookmarks Bar (y=59, x=20+165=185)
# Mouse is at (160, 100). dx = 185 - 160 = +25, dy = 59 - 100 = -41.
send_monitor("mouse_move 25 -41")
time.sleep(0.5)
send_monitor("mouse_button 1")
time.sleep(0.1)
send_monitor("mouse_button 0")
time.sleep(6.0)

send_monitor("screendump /tmp/archaos_test/frog.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/frog.ppm"], stdout=open(png_frogfind, "wb"))

# 3. Click [Home] bookmark (y=59, x=36).
# Mouse is at (185, 59). dx = 36 - 185 = -149, dy = 0.
send_monitor("mouse_move -149 0")
time.sleep(0.5)
send_monitor("mouse_button 1")
time.sleep(0.1)
send_monitor("mouse_button 0")
time.sleep(2.0)

# Scroll down to reveal the HTML5 Video Player
for _ in range(6):
    send_monitor("sendkey down")
    time.sleep(0.15)

time.sleep(1.0)
send_monitor("screendump /tmp/archaos_test/vid.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/vid.ppm"], stdout=open(png_video_stream, "wb"))

proc.terminate()
print("NetSurf verification test complete!")
