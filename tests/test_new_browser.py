import subprocess
import time
import os

os.makedirs("/tmp/archaos_test", exist_ok=True)
serial_log = "/tmp/archaos_test/serial.log"
img_dir = "/home/akushaji/.gemini/antigravity/brain/7c1657c8-11d2-45ed-89b6-04b6729cbc04/images"
os.makedirs(img_dir, exist_ok=True)

png_home = os.path.join(img_dir, "browser_home_new.png")
png_scrolled = os.path.join(img_dir, "browser_scrolled_new.png")
png_js = os.path.join(img_dir, "browser_js_card.png")
png_alert = os.path.join(img_dir, "browser_alert_new.png")

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

# 1. Capture Firefox Home Page (Top view)
send_monitor("screendump /tmp/archaos_test/home.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/home.ppm"], stdout=open(png_home, "wb"))
print("Captured Home page")

# 2. Scroll down 5 times to view Media card
for _ in range(5):
    send_monitor("sendkey down")
    time.sleep(0.2)

time.sleep(1.0)
send_monitor("screendump /tmp/archaos_test/scrolled.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/scrolled.ppm"], stdout=open(png_scrolled, "wb"))
print("Captured Scrolled view")

# 3. Scroll down 7 more times to view JavaScript Subsystem card
for _ in range(7):
    send_monitor("sendkey down")
    time.sleep(0.2)

time.sleep(1.0)
send_monitor("screendump /tmp/archaos_test/js_card.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/js_card.ppm"], stdout=open(png_js, "wb"))
print("Captured JS Card view")

# 4. Press Tab 10 times to focus the Test JavaScript Alert button and press Enter
for _ in range(10):
    send_monitor("sendkey tab")
    time.sleep(0.2)

time.sleep(0.5)
send_monitor("sendkey ret")
time.sleep(1.0)

send_monitor("screendump /tmp/archaos_test/alert.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/alert.ppm"], stdout=open(png_alert, "wb"))
print("Captured Alert view")

proc.terminate()
print("Test complete!")
