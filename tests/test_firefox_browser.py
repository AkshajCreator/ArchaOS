import subprocess
import time
import os

os.makedirs("/tmp/archaos_test", exist_ok=True)
serial_log = "/tmp/archaos_test/serial.log"
png_tab1 = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/firefox_home_tab1.png"
png_tab2 = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/firefox_tabs_multitab.png"
png_tab2_ddg = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/firefox_tab2_ddg.png"
png_video_play = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/firefox_video_play.png"

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

# 1. Capture Firefox Home Tab 1
send_monitor("screendump /tmp/archaos_test/tab1.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/tab1.ppm"], stdout=open(png_tab1, "wb"))

# 2. Click [+] to open a 2nd tab (tab 1 is at x=24..114, plus is at x=118, y=15+18=33)
# Mouse is at (160, 100). dx = 122 - 160 = -38, dy = 32 - 100 = -68.
send_monitor("mouse_move -38 -68")
time.sleep(0.5)
send_monitor("mouse_button 1")
time.sleep(0.1)
send_monitor("mouse_button 0")
time.sleep(1.0)

# Capture 2-tab state
send_monitor("screendump /tmp/archaos_test/tab2.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/tab2.ppm"], stdout=open(png_tab2, "wb"))

# 3. In Tab 2, click [DDG Lite] on the Bookmarks Bar (y=15+44=59, x=20+120=140)
# Mouse is at (122, 32). dx = 140 - 122 = +18, dy = 59 - 32 = +27.
send_monitor("mouse_move 18 27")
time.sleep(0.5)
send_monitor("mouse_button 1")
time.sleep(0.1)
send_monitor("mouse_button 0")
time.sleep(7.0)

# Capture Tab 2 DuckDuckGo search results
send_monitor("screendump /tmp/archaos_test/tab2_ddg.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/tab2_ddg.ppm"], stdout=open(png_tab2_ddg, "wb"))

proc.terminate()
print("Firefox browser test complete!")
