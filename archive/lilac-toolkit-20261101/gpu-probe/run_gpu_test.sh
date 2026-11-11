#!/bin/bash
# One-command GPU test: patch -> boot -> 4 foreground camera frames + USB.
# Camera discipline: at most 4 single frames per boot, spaced >=25s.
# usage: bash research/run_gpu_test.sh <img> <tag>
set -u
cd "$(dirname "$0")/.." || exit 1
IMG="${1:?image name}"
TAG="${2:-test}"
TMP="${TEMP:-/c/Users/Cisco He/AppData/Local/Temp}"
CAM="http://192.168.68.60:4747/cam/1/frame.jpg"
LOG="$TMP/$TAG-cam.log"
export MSYS_NO_PATHCONV=1
: > "$LOG"

if ! fastboot devices 2>/dev/null | grep -q BH905ND69G; then
	echo "phone is not in fastboot"; exit 1
fi

patch_once() {
	(cd unlock && ./xperable.exe -B -U -s 0xf49880 -x -4 -6 2>&1)
}
P=$(patch_once); C=$(echo "$P" | grep -c 'patched successfully')
if [ "$C" -lt 2 ]; then
	echo "patch stalled ($C/2); refreshing fastboot session"
	fastboot reboot bootloader >/dev/null 2>&1; sleep 10
	P=$(patch_once); C=$(echo "$P" | grep -c 'patched successfully')
fi
if [ "$C" -lt 2 ]; then
	echo "patch NOT confirmed; refusing to boot"; echo "$P" | tail -3; exit 1
fi
echo "patched 2/2"

frame() {
	local tag="$1"
	bash research/grab_stream.sh "$TMP/$TAG-$tag.jpg" >> "$LOG" 2>&1
	echo "$(date +%T) frame $tag size=$(stat -c %s "$TMP/$TAG-$tag.jpg" 2>/dev/null || echo 0)" >> "$LOG"
}

(cd unlock && ./xperable.exe -I "../$IMG" -1 -c boot -1) || true
BOOT=$(date +%s)
last=0
for spec in 70; do
	now=$(( $(date +%s) - BOOT ))
	d=$(( spec - now )); [ $d -gt 0 ] && sleep $d
	frame $spec
	N=$(fastboot devices 2>/dev/null | grep -c BH905ND69G)
	echo "$(date +%T) t=${spec}s fastboot=$N" >> "$LOG"
	last=$spec
done
for i in $(seq 1 5); do sleep 2; known=$(fastboot devices 2>/dev/null | grep -c BH905ND69G); echo "$(date +%T) tail fastboot=$known" >> "$LOG"; done
cat "$LOG"
ls -l "$TMP/$TAG"-70.jpg 2>/dev/null
