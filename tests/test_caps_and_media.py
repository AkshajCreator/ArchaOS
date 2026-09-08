import subprocess
import time
import os

os.makedirs("/tmp/archaos_test", exist_ok=True)
serial_log = "/tmp/archaos_test/serial.log"
png_home_spacing = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/browser_home_spacing.png"
png_caps_test = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/browser_caps_test.png"
png_video_play = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/browser_video_play.png"
png_ddg_route = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/browser_ddg_route.png"

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
time.sleep(4.0)

# Capture initial home layout with new margins & padding
send_monitor("screendump /tmp/archaos_test/home.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/home.ppm"], stdout=open(png_home_spacing, "wb"))

# Tab into search input
send_monitor("sendkey tab")
time.sleep(0.3)

# Test Caps Lock
send_monitor("sendkey caps_lock")
time.sleep(0.2)

for char in "archaos":
    send_monitor(f"sendkey {char}")
    time.sleep(0.15)

send_monitor("sendkey caps_lock") # Toggle off
time.sleep(0.3)

send_monitor("screendump /tmp/archaos_test/caps.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/caps.ppm"], stdout=open(png_caps_test, "wb"))

# Clear search box using Backspace
for _ in range(10):
    send_monitor("sendkey backspace")
    time.sleep(0.1)

# Tab to video element (tab 7 times to reach video)
for _ in range(7):
    send_monitor("sendkey tab")
    time.sleep(0.2)

# Press Enter on video to PLAY
send_monitor("sendkey ret")
time.sleep(2.5)

send_monitor("screendump /tmp/archaos_test/video.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/video.ppm"], stdout=open(png_video_play, "wb"))

# Focus URL bar and visit duckduckgo.com
send_monitor("sendkey esc")
time.sleep(0.2)
send_monitor("sendkey tab") # focus URL bar
time.sleep(0.2)

for _ in range(15):
    send_monitor("sendkey backspace")
    time.sleep(0.1)

for char in "duckduckgo.com":
    if char == ".":
        send_monitor("sendkey dot")
    else:
        send_monitor(f"sendkey {char}")
    time.sleep(0.12)

send_monitor("sendkey ret")
time.sleep(6.0)

send_monitor("screendump /tmp/archaos_test/ddg.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/ddg.ppm"], stdout=open(png_ddg_route, "wb"))

proc.terminate()
print("Comprehensive test completed!")
