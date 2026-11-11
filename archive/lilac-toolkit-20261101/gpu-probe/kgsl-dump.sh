#!/usr/bin/env bash
# Dump the full KGSL/SMMU secure+user state from a rooted Android running
# boot-adb-force-lilac.img. This is the "Sony TZ answer table" the mentor
# asked for: SMR/S2CR per stream, every context bank, TSB/trust, CP regs,
# and the GPU VA of ring/microcode.
# usage: bash research/kgsl-dump.sh [OUTDIR]
set -u
OUT="${1:-$TEMP/kgsl-dump-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
export MSYS_NO_PATHCONV=1
D=BH905ND69G
DEV=kgsl-3d0

adbs() { adb -s $D "$@"; }

if ! adbs devices 2>/dev/null | grep -q "device$"; then
	echo "adb not up yet"; exit 1
fi

adbs shell 'id' > "$OUT/id.txt" 2>&1
adbs shell 'getprop ro.product.model; getprop ro.board.platform; getprop ro.build.fingerprint' > "$OUT/props.txt" 2>&1
adbs shell 'dmesg' > "$OUT/dmesg.txt" 2>&1
adbs shell 'ls -la /sys/fs/pstore 2>/dev/null; cat /sys/fs/pstore/console-ramoops-0 2>/dev/null' > "$OUT/pstore.txt" 2>&1
adbs shell 'ls -la /d 2>/dev/null; ls -la /d/kgsl 2>/dev/null; ls -la /d/iommu 2>/dev/null; ls -la /d/iommu/arm-smmu* 2>/dev/null' > "$OUT/dbgfs-list.txt" 2>&1

# kgsl debugfs register dumps (names vary by kernel)
for f in registers gpu_model gpu_clock gpu_busy gpu_freq memtrace; do
	adbs shell "cat /d/kgsl/$DEV/$f 2>/dev/null" > "$OUT/kgsl-$f.txt" 2>&1
done

# arm-smmu qcom debugfs: registers + every domain's ATOS/regs
adbs shell 'for d in /d/iommu/arm-smmu*; do echo "== $d =="; cat $d/regs 2>/dev/null; done' > "$OUT/smmu-regs.txt" 2>&1
adbs shell 'for d in /d/iommu/*/domain*; do echo "== $d =="; ls $d; cat $d/regs 2>/dev/null; cat $d/attrs 2>/dev/null; done' > "$OUT/smmu-domains.txt" 2>&1

# If devmem exists and /dev/mem is usable, read the raw windows directly.
if adbs shell 'which devmem || ls /system/bin/devmem /data/local/tmp/devmem 2>/dev/null' | grep -q devmem; then
	: > "$OUT/regs-raw.txt"
	add() { adbs shell "devmem $1 32" >> "$OUT/regs-raw.txt" 2>&1; }
	# SMMU GR0: SMR0..7, S2CR0..7, ID regs
	for a in 0x05040800 0x05040804 0x05040808 0x0504080c 0x05040810 0x05040814 0x05040818 0x0504081c \
	         0x05040c00 0x05040c04 0x05040c08 0x05040c0c 0x05040c10 0x05040c14 0x05040c18 0x05040c1c \
	         0x05040020 0x05040024 0x05040028 0x05040000; do add $a; done
	# CB0..CB4 SCTLR/TTBR0/TCR/FSR
	for cb in 0x0000 0x1000 0x2000 0x3000 0x4000; do
		base=$((0x05048000 + cb))
		for off in 0x00 0x20 0x30 0x58; do
			printf -v a '0x%x' $((base + off)); add $a
		done
	done
	# GPU SECVID + CP + RBBM
	for a in 0x05000000 0x05000010 0x0500002c 0x05000038 0x0500037c 0x05004e1c 0x0500f000 0x0500f001 0x0500f002 0x0500f003 0x0500f400 0x050014c0 0x05001500 0x05001504 0x05001a88 0x05001b0c 0x05001b18 0x05001b1a; do
		add $a
	done
	# CP: ME/RB/INSTR bases
	for a in 0x05002000 0x05002008 0x0500200c 0x05002018 0x0500201c 0x05002010 0x05002014 0x05002080 0x05002084 0x05002088 0x0500208c 0x05002090 0x05002094; do
		add $a
	done
fi

# GPU VA of microcode/ring as kgsl sees them
adbs shell 'cat /d/kgsl/'$DEV'/mem 2>/dev/null' > "$OUT/kgsl-mem.txt" 2>&1
adbs shell 'cat /proc/iomem | grep -i "kgsl\|gpu\|iommu"' > "$OUT/iomem.txt" 2>&1

echo "dumped to $OUT"
ls -la "$OUT"
