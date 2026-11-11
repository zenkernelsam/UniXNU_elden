#!/usr/bin/env bash
# fire_harvest.sh IMG CELL_PA_BASE [KERNEL_MACHO]
# One bring-up cycle: clear PON if in Android, patch ABL, boot IMG once,
# wait for the kernel to reset to fastboot, re-patch, dump the DDR panic
# cell (PARG/caller/string), then reboot to Android for PON harvest.
# Never runs `fastboot reboot bootloader` when already in fastboot (E1F4 risk).
set -u
export MSYS_NO_PATHCONV=1
ADB="D:/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe"
FB="D:/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/fastboot.exe"
UNLOCK="D:/Desktop/Git/Patch/Xperia/unlock"
IMG="${1:?usage: fire_harvest.sh IMG CELL_PA_BASE [MACHO]}"
CELL="${2:?cell pa base}"
MACHO="${3:-}"
SER=BH905ND69G
DUMP="C:/Users/Cisco He/AppData/Local/Temp/ddr_cell_$$.bin"
ok=0

mode() {
  if [ "$("$ADB" devices | grep -c "$SER[[:space:]]*device$")" != 0 ]; then echo adb; return; fi
  if [ "$("$FB" devices | grep -c "$SER")" != 0 ]; then echo fb; return; fi
  echo none
}

patch() {
  cd "$UNLOCK" || return 1
  local p
  p=$(./xperable.exe -B -U -s 0xf49880 -x -4 -6 2>&1 | grep -c "patched successfully")
  [ "$p" = 2 ]
}

echo "[*] start $(date +%H:%M:%S) mode=$(mode)"

if [ "$(mode)" = adb ]; then
  echo "[*] clearing PON baseline"
  { sleep 4; printf '%s\nexit\n' 'echo "88e 00" > /sys/kernel/debug/regmap/spmi0-00/registers; sleep 1; echo 0x88e > /sys/kernel/debug/regmap/spmi0-00/address; cat /sys/kernel/debug/regmap/spmi0-00/data'; } \
    | "$ADB" -s "$SER" shell /data/local/tmp/bindershell 2>/dev/null | tr -d '\r' | tail -1
  "$ADB" -s "$SER" reboot bootloader
  sleep 10
fi

if [ "$(mode)" != fb ]; then
  e1=$(powershell -NoProfile -Command "Get-PnpDevice -PresentOnly | Where-Object InstanceId -like '*VID_0FCE*' | ForEach-Object { \$_.InstanceId }" 2>/dev/null | grep -c "PID_E1F4")
  if [ "$e1" != 0 ]; then
    echo "[!] phone is in E1F4 charger mode (USB mass storage only)."
    echo "    Recovery needs a PHYSICAL key press: keep USB attached and hold"
    echo "    Volume-Up + Power for ~10s; keep holding Vol-Up through the"
    echo "    reboot to land directly in fastboot."
  else
    echo "[!] not in fastboot (mode=$(mode)) and no E1F4 device visible; check cable/port"
  fi
  exit 2
fi

for t in 1 2 3; do
  if patch; then ok=1; echo "[*] patched (try $t)"; break; fi
  echo "[!] patch failed (try $t)"
  sleep 3
done
if [ "$ok" != 1 ]; then
  echo "[!] patch still failing; leaving session alone"
  exit 3
fi

cd "$UNLOCK" || exit 1
./xperable.exe -I "$IMG" -1 -c boot -1 >/dev/null 2>&1
sleep 5
jumped=0
[ "$("$FB" devices | grep -c "$SER")" = 0 ] && jumped=1
echo "[*] fire $(date +%H:%M:%S) jumped=$jumped"
if [ "$jumped" = 0 ]; then
  ./xperable.exe -I "$IMG" -1 -c boot -1 >/dev/null 2>&1
  sleep 5
  [ "$("$FB" devices | grep -c "$SER")" = 0 ] && jumped=1
  echo "[*] refire jumped=$jumped"
fi

# Wait up to TICKS*10 s for the kernel to die/reset. Prefer fastboot (DDR
# cell readable), but also accept a direct return to Android: in that case
# the PON reading must be taken BEFORE anything else, or it would be lost.
# After a successful root mount the kernel stays up ~15 min, so set
# WAIT_TICKS=110 for post-flash runs.
TICKS="${WAIT_TICKS:-36}"
back=""
for i in $(seq 1 "$TICKS"); do
  sleep 10
  if [ "$("$FB" devices | grep -c "$SER")" != 0 ]; then
    back=fb; echo "[*] back in fastboot $(date +%H:%M:%S)"; break
  fi
  if [ "$("$ADB" devices | grep -c "device$")" != 0 ]; then
    back=adb; echo "[*] back in Android $(date +%H:%M:%S)"; break
  fi
done

if [ "$back" = fb ]; then
  for t in 1 2 3; do
    if patch; then echo "[*] re-patched for DDR read (try $t)"; break; fi
    echo "[!] re-patch failed (try $t)"; sleep 3
  done
  cd "$UNLOCK" || exit 1
  ./xperable.exe -a "$CELL" -s 512 -r -O "$DUMP" >/dev/null 2>&1
  echo "[*] DDR cell dump:"
  python "$(dirname "$0")/ddr_cell.py" "$DUMP" 0 "$MACHO" 2>/dev/null || echo "    parse failed ($DUMP)"
  echo "[*] raw dump kept at $DUMP"
elif [ "$back" = adb ]; then
  echo "[*] returned to Android: DDR cell unavailable (needs fastboot); reading PON first"
else
  echo "[!] no adb/fastboot within timeout"
fi

# Read PON via bindershell (no clearing here: this is the result we want).
pon_read() {
  { sleep 4; printf '%s\nexit\n' 'echo 0x88e > /sys/kernel/debug/regmap/spmi0-00/address; cat /sys/kernel/debug/regmap/spmi0-00/data'; } \
    | "$ADB" -s "$SER" shell /data/local/tmp/bindershell 2>/dev/null | tr -d '\r' | tail -1
}

if [ "$back" = fb ] && [ "${NO_ANDROID:-0}" != 1 ]; then
  # Go to Android only after the DDR cell has been read. NO_ANDROID=1 when
  # userdata now holds the iOS root (Android cannot boot).
  "$FB" reboot
  sleep 30
  "$ADB" -s "$SER" wait-for-device 2>/dev/null
fi
if [ "$("$ADB" devices | grep -c "device$")" != 0 ]; then
  echo "[*] PON:"
  PON_LINE=$(pon_read)
  echo "    $PON_LINE"
  HEX=$(echo "$PON_LINE" | grep -o '088e: [0-9a-fA-F]*' | awk '{print $2}')
  if [ -n "$HEX" ]; then python "$(dirname "$0")/pon_steps.py" "0x$HEX" 2>/dev/null || true; fi
fi
echo "[*] done $(date +%H:%M:%S)"
