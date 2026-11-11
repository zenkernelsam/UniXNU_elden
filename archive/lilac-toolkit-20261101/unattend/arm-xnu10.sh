#!/bin/bash
# arm-xnu10.sh — persistent armed sentinel v2.
# Fires the xnu9 telemetry boot the moment BH905ND69G reappears, then
# harvests every telemetry channel. v2 fixes vs arm-xnu9.sh:
#   - recovery ladder (kill-server -> adbreset -> replug) instead of
#     giving up on transient adb death (v1 died exactly this way 07:51)
#   - reboot-verify loop: re-issues `reboot bootloader` if fastboot
#     doesn't appear (v1 assumed one-shot reboot always works)
#   - at fastboot landing: grabs `fastboot getvar all` BEFORE rebooting
#     to Android (ABL vars lost otherwise) and records landing latency
#     (<125s => early kernel death, ~125s => WDT bite)
#   - harvest adds bindershell fmem dump of IMEM mark cells
#     0x146bf200/300/400/500 + official reg, because warmboot= alone is
#     ABL-rewritten on some paths
#   - NEVER exits while phone unseen: loops back to watch state instead
#     of dying on FAIL (device may be mid-recovery)
# Usage: nohup bash arm-xnu10.sh > /dev/null 2>&1 &
export MSYS_NO_PATHCONV=1
ADB="C:/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe"
FB="C:/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/fastboot.exe"
IMG="C:/Users/Cisco He/Desktop/Git/ios-xperia-xz1c/artifacts/shim-xnu10-lilac.img"
BASE="C:/Users/Cisco He/Desktop/Git/Patch/Xperia/unattend"
LOG="$BASE/out/arm-xnu10.log"
DONE="$BASE/out/arm-xnu10.DONE"
BS="/data/local/tmp/bindershell"
SER="BH905ND69G"

say() { echo "$(date '+%F %T') $*" | tee -a "$LOG"; }
adb_state() { timeout 8 "$ADB" devices 2>/dev/null | grep "$SER" | awk '{print $2}'; }
fb_state()  { timeout 8 "$FB" devices 2>/dev/null | grep -c "$SER"; }
usbctl()    { echo "$1" > "$BASE/cmd/sent-$RANDOM.cmd"; }

rm -f "$DONE"
say "armed v2 — waiting for $SER (img=$IMG)"

# ---- Phase 1: wait for ANY sighting, with recovery ladder -----------
miss=0
while true; do
  s=$(adb_state); f=$(fb_state)
  [ "$s" = "device" ] && { say "adb=DEVICE"; break; }
  [ "$f" = "1" ]      && { say "fastboot seen"; break; }
  miss=$((miss+1))
  if [ "$s" = "offline" ]; then
    say "adb offline — escalating (miss=$miss)"
    "$ADB" kill-server 2>/dev/null; sleep 3; "$ADB" start-server >/dev/null 2>&1
    [ $((miss % 4)) -eq 0 ] && usbctl adbreset
    [ $((miss % 8)) -eq 0 ] && usbctl replug
  elif [ "$miss" -gt 0 ] && [ $((miss % 20)) -eq 0 ]; then
    say "still unseen after $miss polls — usbctl replug"
    usbctl replug
  fi
  sleep 12
done

# ---- Phase 2: ensure fastboot (reboot-verify loop) -------------------
if [ "$(fb_state)" != "1" ]; then
  tries=0
  while true; do
    tries=$((tries+1))
    [ $tries -gt 5 ] && { say "FAIL: no fastboot after $tries reboot attempts — re-arming watch"; exec "$0"; }
    s=$(adb_state)
    if [ "$s" = "device" ]; then
      say "reboot bootloader (try $tries)"
      timeout 10 "$ADB" -s "$SER" reboot bootloader
    else
      say "adb state=$s while waiting for fastboot (try $tries)"
    fi
    n=0
    while [ "$(fb_state)" != "1" ] && [ $n -lt 6 ]; do n=$((n+1)); sleep 5; done
    [ "$(fb_state)" = "1" ] && break
  done
fi
say "in fastboot — snapshot getvar"
timeout 15 "$FB" getvar all 2>&1 | tee -a "$LOG"
sleep 2

# ---- Phase 3: fire ---------------------------------------------------
say "BOOT: fastboot boot $IMG"
t0=$SECONDS
timeout 60 "$FB" -s "$SER" boot "$IMG" 2>&1 | tee -a "$LOG"
say "boot command returned after $((SECONDS-t0))s"

# ---- Phase 4: watch landing — record WHERE and WHEN -----------------
landed=""
for i in $(seq 1 40); do
  f=$(fb_state); s=$(adb_state)
  if [ "$f" = "1" ] && [ -z "$landed" ]; then
    landed="fastboot@${i}x5s"
    say "LANDED fastboot at +$((i*5))s $([ $((i*5)) -lt 120 ] && echo '(EARLY death, not WDT)' || echo '(WDT window)')"
    timeout 15 "$FB" getvar all 2>&1 | tee -a "$LOG"
  fi
  if [ "$s" = "device" ]; then landed="${landed:-adb@${i}x5s}"; say "adb up at +$((i*5))s"; break; fi
  sleep 5
done
[ -z "$landed" ] && say "no landing in 200s — device vanished? continue watching anyway"

# If we landed in fastboot only, boot to Android for harvest.
# NOTE 2026-10-22: this transition powered the phone off once. Give it
# extra time and keep watching for re-appearance rather than assuming dead.
if [ "$(adb_state)" != "device" ] && [ "$(fb_state)" = "1" ]; then
  say "rebooting to Android for harvest"
  timeout 15 "$FB" -s "$SER" reboot
fi

n=0
while [ "$(adb_state)" != "device" ]; do
  n=$((n+1))
  if [ $n -gt 36 ]; then
    say "FAIL: no adb 6min — phone likely powered off (E1F4 pattern)."
    say "leaving armed — will refire if device reappears"
    exec "$0"
  fi
  [ $((n % 6)) -eq 0 ] && { say "adb wait ${n}0s — state=$(adb_state) fb=$(fb_state)"; "$ADB" kill-server 2>/dev/null; sleep 2; "$ADB" start-server >/dev/null 2>&1; }
  sleep 10
done
say "adb back — harvesting"
sleep 10

# ---- Phase 5: telemetry harvest --------------------------------------
CL=$(timeout 10 "$ADB" -s "$SER" shell cat /proc/cmdline 2>/dev/null | tr -d '\r')
echo "cmdline: $CL" | tee -a "$LOG"
WB=$(echo "$CL" | grep -o 'warmboot=0x[0-9a-fA-F]*')
say "warmboot: ${WB:-NOT FOUND}"

case "$WB" in
  "warmboot=0x77665501") say "DECODE: loader jumped, kernel left NO mark — dead before _start or fetch-abort" ;;
  "warmboot=0x7766E000") say "DECODE: reached _start, died before start_first_cpu" ;;
  "warmboot=0x7766E101") say "DECODE: passed start_first_cpu entry, died post-DAIF" ;;
  "warmboot=0x7766E102") say "DECODE: died in E102->AA01 sliver (near-impossible window)" ;;
  "warmboot=0x7766AA01") say "DECODE: PASSED Stage-0! check pstore for C-domain marks" ;;
  warmboot=0x7766F0*)   say "DECODE: EXCEPTION caught by shim vector: $WB" ;;
  warmboot=0x77664D*)   say "DECODE: kernel stage mark $WB" ;;
  warmboot=0x776650*)   say "DECODE: kernel PANIC mark $WB" ;;
  *)                    say "DECODE: unknown/absent" ;;
esac

# pstore (unpriv + su + bindershell attempts)
timeout 10 "$ADB" -s "$SER" shell 'ls -la /sys/fs/pstore/ 2>/dev/null' | tee -a "$LOG"
timeout 15 "$ADB" -s "$SER" shell 'for f in /sys/fs/pstore/*; do echo "=== $f"; tail -30 "$f" 2>/dev/null; done' 2>/dev/null | tee -a "$LOG"

# bindershell: IMEM mark cells via /dev/fmem
{ sleep 4; printf 'id\n'; sleep 1;
  printf 'for a in 146bf65c 146bf200 146bf300 146bf400 146bf500; do echo "== $a"; dd if=/dev/fmem bs=4 count=1 skip=$((0x$a/4)) 2>/dev/null | od -A x -t x4; done\n';
  printf 'exit\n'; sleep 2; } | timeout 30 "$ADB" -s "$SER" shell "$BS" 2>/dev/null | tee -a "$LOG"

touch "$DONE"
say "DONE — one-shot complete"
