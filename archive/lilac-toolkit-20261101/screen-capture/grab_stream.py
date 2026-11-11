#!/usr/bin/env python3
"""Grab one JPEG from DroidCam's MJPEG stream and close immediately.

usage: grab_stream.py OUT [seconds] [url]
Opens /video?1920x1080 for at most `seconds`, extracts the first complete
JPEG (FFD8..FFD9) from the multipart stream, writes it to OUT.
"""
import sys
import urllib.request

out = sys.argv[1] if len(sys.argv) > 1 else '/tmp/stream.jpg'
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0
url = sys.argv[3] if len(sys.argv) > 3 else 'http://192.168.68.60:4747/video?1920x1080'

buf = bytearray()
try:
    with urllib.request.urlopen(url, timeout=secs) as r:
        while len(buf) < 8 * 1024 * 1024:
            chunk = r.read(65536)
            if not chunk:
                break
            buf += chunk
            start = buf.find(b'\xff\xd8')
            if start >= 0:
                end = buf.find(b'\xff\xd9', start + 2)
                if end > start:
                    with open(out, 'wb') as f:
                        f.write(buf[start:end + 2])
                    print('ok', end - start + 2)
                    sys.exit(0)
    print('no jpeg', len(buf))
    sys.exit(1)
except Exception as e:
    # A read timeout still leaves a usable buffer.
    start = buf.find(b'\xff\xd8')
    end = buf.find(b'\xff\xd9', start + 2) if start >= 0 else -1
    if start >= 0 and end > start:
        with open(out, 'wb') as f:
            f.write(buf[start:end + 2])
        print('ok-after-timeout', end - start + 2)
        sys.exit(0)
    print('fail', type(e).__name__, e)
    sys.exit(1)
