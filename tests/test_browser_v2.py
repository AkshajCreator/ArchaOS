import subprocess
import time
import os

os.makedirs("/tmp/archaos_test", exist_ok=True)
serial_log = "/tmp/archaos_test/serial.log"
png_home_white = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/browser_home_white.png"
png_hover_hand = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/browser_hover_hand.png"
png_hover_ibeam = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/browser_hover_ibeam.png"

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

# Capture clean white home page
send_monitor("screendump /tmp/archaos_test/home_white.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/home_white.ppm"], stdout=open(png_home_white, "wb"))

# Move mouse over [Google Lite] bookmark (x=70, y=42) to test Hand cursor
# Absolute mouse coordinates for QEMU: 0..0x7fff (32767)
# 70/320 * 32767 = 7167, 42/200 * 32767 = 6881
send_monitor("mouse_move 7167 6881 0")
time.sleep(1.0)
send_monitor("screendump /tmp/archaos_test/hover_hand.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/hover_hand.ppm"], stdout=open(png_hover_hand, "wb"))

# Move mouse over URL bar (x=120, y=28) to test I-Beam cursor
# 120/320 * 32767 = 12287, 28/200 * 32767 = 4587
send_monitor("mouse_move 12287 4587 0")
time.sleep(1.0)
send_monitor("screendump /tmp/archaos_test/hover_ibeam.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/hover_ibeam.ppm"], stdout=open(png_hover_ibeam, "wb"))

proc.terminate()
print("Browser v2 test complete!")
