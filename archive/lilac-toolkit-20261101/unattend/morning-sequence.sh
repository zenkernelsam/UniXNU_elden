#!/usr/bin/env bash
# morning-sequence.sh — 2026-10-24 清晨一键恢复+取证+再发射
# 前提：手机已从充电模式按电源键回到 Android（adb 可见）
# 用法（Git Bash）：bash unattend/morning-sequence.sh
set -u
export MSYS_NO_PATHCONV=1
ADB="/c/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe"
FB="/c/Users/Cisco He/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/fastboot.exe"
XP="/c/Users/Cisco He/Desktop/Git/Patch/Xperia/unlock/xperable.exe"
IMG="/c/Users/Cisco He/Desktop/Git/Patch/Xperia/shim-dog65-v2-gzdtb.img"
DEV=BH905ND69G
STAMP=$(date +%H%M%S)
LOG="/c/Users/Cisco He/Desktop/Git/Patch/Xperia/unattend/harvest-$STAMP.txt"

rsh() { { sleep 4; printf '%s\nexit\n' "$1"; } | "$ADB" -s $DEV shell /data/local/tmp/bindershell 2>&1; }

echo "=== [$STAMP] step0: 等 adb 设备 ==="
"$ADB" -s $DEV wait-for-device
sleep 8   # adbalways.sh 兜底 + bindershell 就绪

echo "=== step1: 收割昨晚裸跑残留（先收再清！） ==="
rsh 'insmod /data/local/tmp/fmem.ko 2>/dev/null
echo "== PON 门铃 =="
grep -E "088[9abcdef]" /sys/kernel/debug/regmap/spmi0-00/registers
echo "== POST cell 0xffbff000 =="
dd if=/dev/fmem bs=4096 skip=1043967 count=1 of=/data/local/tmp/post.bin 2>/dev/null
od -An -tx1 -N8 /data/local/tmp/post.bin
echo "== ramoops prz 0xffc07000 =="
dd if=/dev/fmem bs=4096 skip=1043960 count=8 of=/data/local/tmp/prz.bin 2>/dev/null
strings /data/local/tmp/prz.bin | head -20' | tee "$LOG"

echo "=== step2: 清 PON 基线 ==="
rsh 'echo 1 > /sys/kernel/debug/regmap/spmi0-00/count
for a in 088e 088d 088c 088b 088a 0889; do echo $a:00 > /sys/kernel/debug/regmap/spmi0-00/registers; done
grep -E "088[9e]" /sys/kernel/debug/regmap/spmi0-00/registers' | tee -a "$LOG"

echo "=== step3: reboot bootloader ==="
rsh 'reboot bootloader' >/dev/null 2>&1
sleep 15
"$FB" devices

echo "=== step4: ABL patch（必须两条 successfully） ==="
OUT=$("$XP" -B -U -s 0xf49880 -x -4 -6 2>&1)
echo "$OUT"
echo "$OUT" | grep -c "patched successfully" | grep -q 2 || { echo "!! PATCH 未全成 — 中止，勿发射"; exit 1; }

echo "=== step5: 发射 $IMG ==="
"$XP" -I "$IMG" -1 -c boot -1 2>&1 | tail -3
echo "fired $(date +%H:%M:%S) — 65s 长绳兜底，等回落"

for i in $(seq 1 12); do
  sleep 20
  A=$("$ADB" devices | grep $DEV); F=$("$FB" devices | grep $DEV)
  echo "$(date +%H:%M:%S) adb='${A:0:30}' fb='$F'"
  [ -n "$A$F" ] && break
done

echo "=== step6: 收割本轮 ==="
[ -n "$A" ] && rsh 'insmod /data/local/tmp/fmem.ko 2>/dev/null
grep -E "088[9abcdef]" /sys/kernel/debug/regmap/spmi0-00/registers
dd if=/dev/fmem bs=4096 skip=1043967 count=1 of=/data/local/tmp/post.bin 2>/dev/null
od -An -tx1 -N8 /data/local/tmp/post.bin' | tee -a "$LOG"
echo "=== 完成，日志 $LOG ==="
