#!/usr/bin/env bash
# Read-only kgsl ground-truth probe under TWRP (SUPERVISED ONLY).
#
# Known behavior: standard fastboot-booted images (TWRP/insecure/adb-force)
# die into the E1F4 charge screen ~40-90 s after adbd appears. This script
# therefore collects a minimal, prioritized set within ~35 s and forces
# `adb reboot bootloader`. /data is unmounted first; nothing is written to
# UFS. Do not run while the phone must stay unattended: if adb does not
# appear, the phone will sit in TWRP until a power press.
#
# usage: CONFIRM=1 bash research/twrp_kgsl_probe.sh
set -u
cd "$(dirname "$0")/.." || exit 1
export MSYS_NO_PATHCONV=1

if [ "${CONFIRM:-0}" != "1" ]; then
	echo "REFUSE: supervised-only probe; set CONFIRM=1"
	exit 1
fi
IMG="research/twrp-lilac.img"
TMP="${TEMP:-C:/Users/Cisco He/AppData/Local/Temp}"
OUT="$TMP/twrp-kgsl"
mkdir -p "$OUT"

if ! fastboot devices 2>/dev/null | grep -q BH905ND69G; then
	echo "REFUSE: no fastboot device"; exit 1
fi

P=$( (cd unlock && ./xperable.exe -B -U -s 0xf49880 -x -4 -6 2>&1) )
if [ "$(echo "$P" | grep -c 'patched successfully')" -ne 2 ]; then
	echo "REFUSE: patch failed"; echo "$P" | tail -3; exit 2
fi
(cd unlock && ./xperable.exe -I "../$IMG" -1 -c boot -1) || true

START=$(date +%s)
for i in $(seq 1 30); do
	if adb devices 2>/dev/null | grep -q 'device'; then break; fi
	sleep 1
done
if ! adb devices 2>/dev/null | grep -q 'device'; then
	echo "ALERT: adbd never appeared; phone may be in TWRP (power press needed)"
	exit 3
fi
echo "adbd up at +$(( $(date +%s) - START ))s"

# Never let TWRP hold userdata rw; collect from host stdout only.
adb shell 'umount /data 2>/dev/null; umount /sdcard 2>/dev/null' >/dev/null 2>&1
adb shell 'mount -t debugfs none /sys/kernel/debug 2>/dev/null' >/dev/null 2>&1

adb exec-out sh -c 'dmesg' > "$OUT/dmesg.txt" 2>&1
adb exec-out sh -c 'cat /proc/version; cat /proc/cmdline; getprop ro.board.platform' > "$OUT/ident.txt" 2>&1
adb exec-out sh -c 'ls -l /sys/class/kgsl/kgsl-3d0 2>&1; echo ---; ls /sys/kernel/debug/kgsl 2>&1' > "$OUT/sysfs.txt" 2>&1
adb exec-out sh -c 'for f in gpu_model gpu_clock_stats devfreq/cur_freq devfreq/governor; do echo "== $f"; cat /sys/class/kgsl/kgsl-3d0/$f 2>&1; done' > "$OUT/kgsl.txt" 2>&1
adb exec-out sh -c 'for f in mmu dev preempt; do echo "== $f"; cat /sys/kernel/debug/kgsl/kgsl-3d0/$f 2>&1 | head -80; done' > "$OUT/debugfs.txt" 2>&1
{
	echo "== devmem probe"
	adb exec-out sh -c 'which devmem; devmem 0x5044000 32; devmem 0x5046000 32' 2>&1
	echo "== busybox devmem"
	adb exec-out sh -c 'busybox devmem 0x5044000 32 2>&1; busybox devmem 0x5046000 32 2>&1' 2>&1
} > "$OUT/regs.txt" 2>&1

echo "elapsed +$(( $(date +%s) - START ))s; returning to fastboot"
adb reboot bootloader >/dev/null 2>&1 || adb shell 'reboot bootloader' >/dev/null 2>&1
for i in $(seq 1 30); do
	if fastboot devices 2>/dev/null | grep -q BH905ND69G; then
		echo "fastboot restored at +$(( $(date +%s) - START ))s"
		break
	fi
	sleep 1
done
if ! fastboot devices 2>/dev/null | grep -q BH905ND69G; then
	echo "ALERT: did not return to fastboot; check USB PID (E1F4 needs power press)"
fi
echo "logs: $OUT"
echo "--- kgsl key lines ---"
grep -aiE 'kgsl|zap|gpmu|smmu|a5xx|secure|iommu' "$OUT/dmesg.txt" 2>/dev/null | tail -40
