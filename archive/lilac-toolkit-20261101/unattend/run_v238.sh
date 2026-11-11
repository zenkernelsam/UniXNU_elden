#!/bin/bash
# One-command v238/v239 test: fastboot check -> patch 2/2 -> boot ->
# wait for COM17 -> read 60 s -> print the CP/GVA result lines.
# usage: bash research/run_v238.sh <img> <tag>
set -u
cd "$(dirname "$0")/.." || exit 1
IMG="${1:?usage: run_v238.sh <img> <tag>}"
TAG="${2:-v238}"
TMP="${TEMP:-C:/Users/Cisco He/AppData/Local/Temp}"
LOG="$TMP/$TAG-com.log"
export MSYS_NO_PATHCONV=1

if ! fastboot devices 2>/dev/null | grep -q BH905ND69G; then
	echo "phone is not in fastboot (charge mode needs a power press)"; exit 1
fi

patch_once() { (cd unlock && ./xperable.exe -B -U -s 0xf49880 -x -4 -6 2>&1); }
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

(cd unlock && ./xperable.exe -I "../$IMG" -1 -c boot -1) || true
sleep 3
if fastboot devices 2>/dev/null | grep -q BH905ND69G; then
	echo "image was rejected / did not jump"; exit 1
fi
echo "booted $IMG; waiting for COM17"

for i in $(seq 1 16); do
	PORTS=$(powershell -NoProfile -Command "[System.IO.Ports.SerialPort]::GetPortNames() -join ','" 2>/dev/null)
	if echo "$PORTS" | grep -q COM17; then
		echo "COM17 up after ~$((i*5))s"; break
	fi
	sleep 5
done
sleep 1
WLOG=$(cygpath -w "$LOG" 2>/dev/null || echo "$LOG")
powershell -ExecutionPolicy Bypass -File research/read_com.ps1 -Port COM17 -Seconds 45 -Out "$WLOG" 2>&1
echo "--- key lines ($LOG) ---"
grep -aE "SWEET GPU: (GVA|CP cfg|CP split|CP release|CP consumed|CP later|SECVID|SCM remote|zap OK|GPMU host|probe|init)" "$LOG" | tail -30
