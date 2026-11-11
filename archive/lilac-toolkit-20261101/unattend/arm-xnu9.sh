#!/bin/bash
# arm-xnu9.sh — one-shot armed sentinel: fire the xnu9 telemetry boot the
# moment the phone reappears (adb or fastboot), then harvest warmboot= and
# pstore. Safe: xnu9 path has NO power-off reachable code (verify spins,
# panic does PSCI RESET, WDT armed at ~125s recovery).
# Usage: bash arm-xnu9.sh &    (runs until it fires once, then exits)
export MSYS_NO_PATHCONV=1
ADB="C:/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe"
FB="C:/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/fastboot.exe"
IMG="C:/Users/Cisco He/Desktop/Git/ios-xperia-xz1c/artifacts/shim-xnu9-lilac.img"
LOG="C:/Users/Cisco He/Desktop/Git/Patch/Xperia/unattend/out/arm-xnu9.log"
DONE="C:/Users/Cisco He/Desktop/Git/Patch/Xperia/unattend/out/arm-xnu9.DONE"
SER="BH905ND69G"

say() { echo "$(date '+%F %T') $*" | tee -a "$LOG"; }
adb_state() { "$ADB" devices 2>/dev/null | grep "$SER" | awk '{print $2}'; }
fb_state() { "$FB" devices 2>/dev/null | grep -c "$SER"; }

rm -f "$DONE"
say "armed — waiting for $SER (img=$IMG)"

# Phase 1: wait for ANY sighting (adb device, or fastboot)
while true; do
  s=$(adb_state); f=$(fb_state)
  if [ "$s" = "device" ]; then say "adb=DEVICE — rebooting to bootloader"; "$ADB" -s "$SER" reboot bootloader; break; fi
  if [ "$f" = "1" ]; then say "fastboot seen"; break; fi
  sleep 15
done

# Phase 2: ensure fastboot
n=0
while [ "$(fb_state)" != "1" ]; do
  n=$((n+1)); [ $n -gt 12 ] && { say "FAIL: no fastboot after adb reboot"; exit 1; }
  sleep 10
done
sleep 3

# Phase 3: fire
say "BOOT: fastboot boot $IMG"
"$FB" boot "$IMG" 2>&1 | tee -a "$LOG"
say "boot command returned rc=$?"

# Phase 4: wait through WDT window, then for adb (~125s bite + ~60s Android)
sleep 130
n=0
while [ "$(adb_state)" != "device" ]; do
  n=$((n+1)); [ $n -gt 24 ] && { say "FAIL: no adb 4min after boot — check device manually"; exit 1; }
  sleep 10
done
say "adb back — harvesting"
sleep 8   # let adbd/magisk settle

# Phase 5: telemetry harvest
CL=$("$ADB" -s "$SER" shell cat /proc/cmdline 2>/dev/null | tr -d '\r')
[ -z "$CL" ] && CL=$("$ADB" -s "$SER" shell su -c 'cat /proc/cmdline' 2>/dev/null | tr -d '\r')
echo "$CL" | tee -a "$LOG" >/dev/null
WB=$(echo "$CL" | grep -o 'warmboot=0x[0-9a-fA-F]*')
say "warmboot: ${WB:-NOT FOUND}"

case "$WB" in
  "warmboot=0x77665501") say "DECODE: loader jumped, kernel left NO mark — dead before _start or fetch-abort" ;;
  "warmboot=0x7766E000") say "DECODE: reached _start, died before start_first_cpu" ;;
  "warmboot=0x7766E101") say "DECODE: passed start_first_cpu entry, died post-DAIF" ;;
  "warmboot=0x7766E102") say "DECODE: died in E102→AA01 sliver (OSLAR/FB removed — near-impossible window)" ;;
  "warmboot=0x7766AA01") say "DECODE: PASSED Stage-0! check pstore/console for C-domain marks" ;;
  warmboot=0x7766F0*)   say "DECODE: EXCEPTION caught by shim vector: $WB" ;;
  warmboot=0x77664D*)   say "DECODE: kernel stage mark $WB — died at that stage" ;;
  warmboot=0x776650*)   say "DECODE: kernel PANIC mark $WB — panic path fired (PSCI RESET)" ;;
  *)                    say "DECODE: unknown/absent — inspect cmdline above" ;;
esac

"$ADB" -s "$SER" shell su -c 'ls -la /sys/fs/pstore/' 2>/dev/null | tee -a "$LOG"
"$ADB" -s "$SER" shell su -c 'cat /sys/fs/pstore/console-ramoops-0 2>/dev/null | tail -40' 2>/dev/null | tee -a "$LOG"

touch "$DONE"
say "DONE — one-shot complete"
