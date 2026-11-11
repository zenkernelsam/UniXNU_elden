#!/bin/bash
# Grab one still from DroidCam: autofocus first, then pull one JPEG from
# the MJPEG stream (user directive 2026-10-27: always AF before capture;
# the stream is the stable method, the single-frame endpoint is fallback).
# usage: grab_stream.sh OUT
set -u
OUT="${1:-/tmp/frame.jpg}"
WOUT=$(cygpath -w "$OUT" 2>/dev/null || echo "$OUT")
rm -f "$OUT"

# 1. Trigger autofocus and give the lens ~2 s to settle.
curl -s -m 5 "http://192.168.68.60:4747/cam/1/af" >/dev/null || true
sleep 2

# 2. Method B (preferred): MJPEG stream, extract the first complete JPEG.
BIN="${OUT}.mjpeg"
WBIN=$(cygpath -w "$BIN" 2>/dev/null || echo "$BIN")
rm -f "$BIN"
curl -s -m 6 -o "$WBIN" "http://192.168.68.60:4747/video?1280x720" || true
if [ -s "$BIN" ]; then
	python - "$WBIN" "$WOUT" <<'PY'
import sys
b, o = sys.argv[1], sys.argv[2]
d = open(b, 'rb').read()
s = d.find(b'\xff\xd8')
e = d.find(b'\xff\xd9', s + 2) if s >= 0 else -1
if s >= 0 and e > s:
    open(o, 'wb').write(d[s:e + 2])
    print('stream ok', e - s + 2)
else:
    print('no jpeg', len(d))
    sys.exit(1)
PY
	rc=$?
	rm -f "$BIN"
	[ $rc -eq 0 ] && exit 0
fi

# 3. Fallback: single frame (server may be busy with a viewer).
curl -s -m 6 -o "$WOUT" "http://192.168.68.60:4747/cam/1/frame.jpg" || true
sz=$(stat -c %s "$OUT" 2>/dev/null || echo 0)
if [ "$sz" -gt 1000 ]; then
	echo "frame.jpg ok $sz"
	exit 0
fi
echo "capture failed"
exit 1
