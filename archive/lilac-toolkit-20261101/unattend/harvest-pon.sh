#!/usr/bin/env bash
# harvest-pon.sh — post-run forensic extraction from Android via PM8998 PON.
#
# Channel: PON spare byte 0x88e survives warm AND crash resets (verified
# 2026-10-24: warm reset kept 0x5a, panic kept 0x7c, XNU run kept 0x02).
# Read path: bindershell root -> /sys/kernel/debug/regmap/spmi0-00/.
# IMEM/DDR marks are DEAD channels (TZ rewrites imem, DDR wiped on reset)
# — do NOT resurrect the old fastboot/IMEM harvest.sh flow.
#
# PON byte decode (write side: shim RR_MARK + xnu 0095 early_stage_write):
#   0x00        cleared baseline / no mark
#   0x02        loader handoff 0x77665502 — jumped to XNU
#   0x10..0x18  loader stage 0x7766D01x (entrychk/fbband/oslar/dump/vec/xnuldrst/prz)
#   0x00..0x14  xnu stage 0x77664Dnn (early mark nn)
#   0x80..0x9f  xnu PANIC at stage (byte & 0x3f)
#   other       unknown — record raw
set -u
ADB="${ADB:-C:/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe}"
export MSYS_NO_PATHCONV=1
SER="${SERIAL:-BH905ND69G}"
OUT="${OUTDIR:-out/pon-$(date +%Y%m%d-%H%M%S)}"; mkdir -p "$OUT"
say(){ echo "[$(date +%H:%M:%S)] $*" | tee -a "$OUT/run.log"; }

BS=/data/local/tmp/bindershell
root(){ "$ADB" -s "$SER" shell "{ sleep 3; printf '%s\nexit\n' \"$(echo "$1" | sed 's/"/\\"/g')\"; } | $BS" 2>/dev/null | tr -d '\r'; }

say "== harvest-pon: waiting for Android adb =="
for i in $(seq 1 90); do
  "$ADB" -s "$SER" get-state 2>/dev/null | grep -q device && break
  sleep 2
done
"$ADB" -s "$SER" get-state 2>/dev/null | grep -q device || { say "no adb device $SER"; exit 1; }

# ensure bindershell on device (push if missing)
"$ADB" -s "$SER" shell "ls $BS >/dev/null 2>&1" || {
  say "pushing bindershell"
  "$ADB" -s "$SER" push "C:/Users/Cisco He/Desktop/Git/Patch/Xperia/unlock/bindershell" "$BS" >/dev/null 2>&1
  "$ADB" -s "$SER" shell "chmod 755 $BS"
}

say "-- PON spare 0x88e --"
PON=$(root 'echo 0x88e > /sys/kernel/debug/regmap/spmi0-00/address; cat /sys/kernel/debug/regmap/spmi0-00/data' | grep -o '088e: [0-9a-fA-F]*' | awk '{print $2}')
PON=${PON:-FAIL}
say "  raw 0x88e = 0x$PON"

case "$PON" in
  00) DEC='baseline/no mark';;
  02) DEC='loader handoff — jumped to XNU (0x77665502)';;
  1[0-8]) DEC="loader stage 0x7766D0$PON (died INSIDE loader, xnu never jumped)";;
  8[0-9a-f]|9[0-9a-f]) DEC="xnu PANIC at stage $((16#$PON & 0x3f))";;
  *) DEC="xnu stage 0x77664D$PON or unknown";;
esac
say "  decode: $DEC"

say "-- warmboot (secondary, coarse enum only) --"
WB=$(root 'grep -o "warmboot=0x[0-9a-f]*" /proc/cmdline')
say "  $WB"

say "-- PON 0x88f routing reg (armed fastboot flag?) --"
PONF=$(root 'echo 0x88f > /sys/kernel/debug/regmap/spmi0-00/address; cat /sys/kernel/debug/regmap/spmi0-00/data' | grep -o '088f: [0-9a-fA-F]*' | awk '{print $2}')
say "  raw 0x88f = 0x${PONF:-FAIL}"

echo "pon_88e=0x$PON pon_88f=0x${PONF:-FAIL} $WB decode=\"$DEC\"" > "$OUT/summary.txt"
say "== done -> $OUT/summary.txt =="
