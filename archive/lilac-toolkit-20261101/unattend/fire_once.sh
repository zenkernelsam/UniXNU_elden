#!/bin/bash
# Single-shot boot helper. One patch, verify 2/2, boot immediately, then wait
# for the kernel CDC console. Refuses to continue on any intermediate error.
# usage: fire_once.sh <img> <tag> [com_seconds]
set -u
cd "$(dirname "$0")/.." || exit 1
IMG="${1:?usage: fire_once.sh <img> <tag> [com_seconds]}"
TAG="${2:-shot}"
SECS="${3:-120}"
TMP="${TEMP:-C:/Users/Cisco He/AppData/Local/Temp}"
LOG="$TMP/$TAG-com.log"
export MSYS_NO_PATHCONV=1

if ! fastboot devices 2>/dev/null | grep -q BH905ND69G; then
	echo "REFUSE: no fastboot device"; exit 1
fi

P=$( (cd unlock && ./xperable.exe -B -U -s 0xf49880 -x -4 -6 2>&1) )
C=$(echo "$P" | grep -c 'patched successfully')
if [ "$C" -ne 2 ]; then
	echo "REFUSE: patch count=$C (need 2); no retry in this session"
	echo "$P" | tail -3
	exit 2
fi
echo "patched 2/2"

if ! fastboot devices 2>/dev/null | grep -q BH905ND69G; then
	echo "REFUSE: fastboot lost after patch"; exit 3
fi

(cd unlock && ./xperable.exe -I "../$IMG" -1 -c boot -1) || true
sleep 3
if fastboot devices 2>/dev/null | grep -q BH905ND69G; then
	echo "REFUSE: image rejected, still in fastboot"; exit 4
fi

# A charge-mode fallback means the transfer aborted: report immediately.
for i in $(seq 1 6); do
	PIDS=$(powershell -NoProfile -Command "Get-PnpDevice -PresentOnly | Where-Object { \$_.InstanceId -match 'VID_0FCE' } | ForEach-Object { \$_.InstanceId }" 2>/dev/null)
	if echo "$PIDS" | grep -q 'PID_E1F4'; then
		echo "ALERT: phone is in E1F4 charge mode (needs a power press)"
		exit 5
	fi
	sleep 3
done

powershell -ExecutionPolicy Bypass -File research/read_com_auto.ps1 -Seconds "$SECS" -Out "$LOG" 2>&1 | tail -1
echo "log: $LOG"
