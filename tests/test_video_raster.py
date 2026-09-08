import subprocess
import time
import os

os.makedirs("/tmp/archaos_test", exist_ok=True)
serial_log = "/tmp/archaos_test/serial.log"
png_media = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/firefox_media_raster.png"
png_playing = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/firefox_video_playing.png"

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

# Scroll down by pressing Down Arrow 6 times to reveal Media Cards
for _ in range(6):
    send_monitor("sendkey down")
    time.sleep(0.2)

time.sleep(1.0)
send_monitor("screendump /tmp/archaos_test/media.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/media.ppm"], stdout=open(png_media, "wb"))

# Click [> PLAY] on the video player (video is around center x=160, y=120)
# Mouse move to center of video player and click!
# Mouse starts at (160, 100). dy = 120 - 100 = +20.
send_monitor("mouse_move 0 20")
time.sleep(0.5)
send_monitor("mouse_button 1")
time.sleep(0.1)
send_monitor("mouse_button 0")
time.sleep(2.0)

# Capture playing state with live animated raster stream!
send_monitor("screendump /tmp/archaos_test/playing.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/playing.ppm"], stdout=open(png_playing, "wb"))

proc.terminate()
print("Video raster test complete!")
