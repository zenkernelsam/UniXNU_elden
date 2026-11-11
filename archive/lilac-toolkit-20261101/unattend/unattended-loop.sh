#!/bin/bash
# unattended-loop.sh — fully autonomous shim/XNU experiment loop for lilac.
#
# Each round (requires phone reachable in fastboot to start):
#   1. xperable ABL runtime patch (-x -4 -6) — dies on every reset, reapply per round
#   2. xperable -I IMG -1 -c boot -1   — fire the image ONCE (ABL jumps ~8-9 s)
#   3. wait for the phone to return to fastboot (shim built with RET=1 arms
#      PON reason=BOOTLOADER before jumping to XNU; any panic/WDT reset
#      routes back here). Hangs stay hung — we abort and wait for the user.
#   4. harvest: read fastboot-visible state + pull ABL log via xperable -l
#
# Usage:  IMG=/path/to/image-gzdtb.img bash unattend/unattended-loop.sh
#         (or edit IMG below). Log: unattend/out/unattended-loop.log
export MSYS_NO_PATHCONV=1
XP="C:/Users/Cisco He/Desktop/Git/Patch/Xperia/unlock/xperable.exe"
FB="C:/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/fastboot.exe"
ADB="C:/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe"
BASE="C:/Users/Cisco He/Desktop/Git/Patch/Xperia/unattend"
LOG="$BASE/out/unattended-loop.log"
SER="BH905ND69G"
IMG="${IMG:-C:/Users/Cisco He/Desktop/Git/ios-xperia-xz1c/artifacts/shim-xnu11-ret-gzdtb.img}"
ROUNDS="${ROUNDS:-999}"
FIRE_DELAY="${FIRE_DELAY:-0}"          # extra settle time after patch, seconds
RETURN_TIMEOUT="${RETURN_TIMEOUT:-180}" # seconds to wait for fastboot return

say() { echo "$(date '+%F %T') $*" | tee -a "$LOG"; }
fb_state() { timeout 8 "$FB" devices 2>/dev/null | grep -c "$SER"; }
adb_state() { timeout 8 "$ADB" devices 2>/dev/null | grep "$SER" | awk '{print $2}'; }

mkdir -p "$BASE/out"
say "=== unattended loop armed: img=$IMG rounds=$ROUNDS ==="

round=0
while [ "$round" -lt "$ROUNDS" ]; do
  round=$((round+1))
  say "--- round $round: waiting for fastboot ---"

  # Phase A: wait until the phone lands in fastboot (60 s cap for boot-up).
  w=0
  until [ "$(fb_state)" = "1" ]; do
    sleep 3; w=$((w+3))
    if [ "$w" -ge 3600 ]; then say "no fastboot for 3600 s — bailing"; exit 2; fi
    if [ "$(adb_state)" = "device" ]; then
      say "adb is up — asking it to enter fastboot"
      "$ADB" -s "$SER" reboot bootloader >/dev/null 2>&1
      sleep 8
    fi
  done
  say "fastboot up (took ${w}s)"

  # Phase B: patch ABL (required every session; dies on reset).
  # xperable needs its PE modules (LinuxLoader-p114.pe) on the CWD.
  say "patching ABL via xperable"
  XPDIR="$(dirname "$XP")"
  if ! (cd "$XPDIR" && "./$(basename "$XP")" -B -U -s 0xf49880 -x -4 -6) >>"$LOG" 2>&1; then
    say "xperable patch FAILED — retrying once"
    sleep 3
    (cd "$XPDIR" && "./$(basename "$XP")" -B -U -s 0xf49880 -x -4 -6) >>"$LOG" 2>&1 || { say "patch twice failed, abort"; exit 3; }
  fi
  [ "$FIRE_DELAY" -gt 0 ] && sleep "$FIRE_DELAY"

  # Phase C: fire the image ONCE. ABL takes ~8-9 s to decompress+jump.
  say "boot $IMG (single shot)"
  (cd "$XPDIR" && "./$(basename "$XP")" -I "$IMG" -1 -c boot -1) >>"$LOG" 2>&1
  sleep 12

  # Phase D: wait for the phone to come back to fastboot.
  say "waiting for return-to-fastboot (timeout ${RETURN_TIMEOUT}s)"
  w=0
  while [ "$(fb_state)" != "1" ]; do
    sleep 3; w=$((w+3))
    # if it went to Android instead of fastboot (PON arm failed), let it boot,
    # then ask adb to bring it back so the loop doesn't stall forever.
    if [ "$(adb_state)" = "device" ]; then
      say "landed in Android (RET arm failed?) — adb reboot bootloader"
      "$ADB" -s "$SER" reboot bootloader >/dev/null 2>&1
      sleep 8
      break
    fi
    if [ "$w" -ge "$RETURN_TIMEOUT" ]; then
      say "TIMEOUT: still not in fastboot after ${RETURN_TIMEOUT}s — likely hung (black screen). ABORT; needs a human."
      exit 4
    fi
  done
  say "back in fastboot after ${w}s"

  # Phase E: fastboot-side forensics — ABL log tail (shows last boot reason).
  (cd "$XPDIR" && "./$(basename "$XP")" -l) >>"$BASE/out/abl-log-r$round.txt" 2>&1 || true
  say "round $round complete; ABL log -> out/abl-log-r$round.txt"
  sleep 2
done
say "=== loop finished: $round rounds ==="
