import subprocess
import time
import os

os.makedirs("/tmp/archaos_test", exist_ok=True)
serial_log = "/tmp/archaos_test/serial.log"
png_logos = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/firefox_multi_website_images.png"
png_video = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/firefox_active_video_3d.png"

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

# Scroll down to reveal images
for _ in range(8):
    send_monitor("sendkey down")
    time.sleep(0.15)

time.sleep(1.0)
send_monitor("screendump /tmp/archaos_test/logos.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/logos.ppm"], stdout=open(png_logos, "wb"))

# Scroll down 8 more times to center the video player
for _ in range(8):
    send_monitor("sendkey down")
    time.sleep(0.15)

time.sleep(0.5)

# Click Play on the Video Player (center of viewport)
# Mouse is at (160, 100). Video play button is around (160, 110). dy = +10.
send_monitor("mouse_move 0 10")
time.sleep(0.5)
send_monitor("mouse_button 1")
time.sleep(0.1)
send_monitor("mouse_button 0")
time.sleep(2.0)

send_monitor("screendump /tmp/archaos_test/video_3d.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/video_3d.ppm"], stdout=open(png_video, "wb"))

proc.terminate()
print("Custom websites test complete!")
