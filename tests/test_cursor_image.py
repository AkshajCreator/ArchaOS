import subprocess
import time
import os

os.makedirs("/tmp/archaos_test", exist_ok=True)
serial_log = "/tmp/archaos_test/serial.log"
png_url_cursor = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/firefox_url_cursor.png"
png_url_scrolled = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/firefox_url_scrolled.png"
png_image_art = "/home/akushaji/.gemini/antigravity/brain/76c8cde7-d985-4437-a945-3df60eb8d22d/images/firefox_graphic_art.png"

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

# 1. Capture initial Home tab with URL cursor
send_monitor("screendump /tmp/archaos_test/url1.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/url1.ppm"], stdout=open(png_url_cursor, "wb"))

# 2. Press Left Arrow 4 times to move inside "about:home", type "X"
for _ in range(4):
    send_monitor("sendkey left")
    time.sleep(0.2)

send_monitor("sendkey shift-x")
time.sleep(0.5)

# Press End to jump to end, then append a long URL string to test horizontal scrolling!
send_monitor("sendkey end")
time.sleep(0.2)
for char in "/very_long_url_test_query_string":
    send_monitor(f"sendkey {char if char.isalnum() else 'slash' if char == '/' else 'minus' if char == '-' else 'shift-minus'}")
    time.sleep(0.1)

time.sleep(0.5)
send_monitor("screendump /tmp/archaos_test/url_scroll.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/url_scroll.ppm"], stdout=open(png_url_scrolled, "wb"))

# 3. Press Esc to unfocus URL bar, then scroll down 7 times to see Graphic Image Artwork
send_monitor("sendkey esc")
time.sleep(0.3)
for _ in range(7):
    send_monitor("sendkey down")
    time.sleep(0.15)

time.sleep(1.0)
send_monitor("screendump /tmp/archaos_test/art.ppm")
time.sleep(0.5)
subprocess.run(["pnmtopng", "/tmp/archaos_test/art.ppm"], stdout=open(png_image_art, "wb"))

proc.terminate()
print("Cursor and image test complete!")
