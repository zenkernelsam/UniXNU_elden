#!/usr/bin/env python3
"""Grab the latest JPEG frame from the DroidCam MJPEG stream.

frame.jpg hangs periodically on this app, but /video?WxH keeps streaming
multipart MJPEG. Usage: grab_cam.py OUT.jpg [SECONDS] [WxH]
"""
import sys, time, urllib.request

out = sys.argv[1]
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0
size = sys.argv[3] if len(sys.argv) > 3 else '1280x720'
url = 'http://192.168.68.60:4747/override'
try:
    urllib.request.urlopen(url, timeout=3).read(1)
except Exception:
    pass
url = 'http://192.168.68.60:4747/video?' + size
req = urllib.request.Request(url, headers={'Accept': 'multipart/x-mixed-replace,image/*'})
data = bytearray()
t0 = time.time()
with urllib.request.urlopen(req, timeout=8) as r:
    while time.time() - t0 < secs:
        chunk = r.read(65536)
        if not chunk:
            break
        data += chunk
        # keep the tail bounded (2 MB is plenty for the last frames)
        if len(data) > (2 << 20):
            del data[:len(data) - (2 << 20)]
end = data.rfind(b'\xff\xd9')
start = data.rfind(b'\xff\xd8', 0, end) if end > 0 else -1
if start < 0 or end < start:
    print('no jpeg in stream (%d bytes)' % len(data))
    raise SystemExit(1)
open(out, 'wb').write(data[start:end + 2])
print('wrote %s (%d bytes)' % (out, end + 2 - start))
