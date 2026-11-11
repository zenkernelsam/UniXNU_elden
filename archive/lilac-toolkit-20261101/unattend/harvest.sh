#!/usr/bin/env bash
# harvest.sh — post-run forensic extraction from fastboot.
# Reads ONLY IMEM/SRAM mark slots via single-word xperable reads.
# HARD RULE: never -r/-w MMIO ranges (0x0xxxxxxx controllers) — an MMIO
# access through ABL's read path crashes it -> reset -> charge mode.
# IMEM 0x146bf000 is on-chip SRAM and ABL reads it itself (restart_reason).
# CAUTION: Android fmem block-reads of IMEM panic the kernel (TZ/XPU);
# keep each read small and only at known slots.
set -u
XP="C:/Users/Cisco He/Desktop/Git/Patch/Xperia/unlock/xperable.exe"
XPDIR="$(dirname "$XP")"
FB="${FASTBOOT:-fastboot}"
SER="${SERIAL:-BH905ND69G}"
OUT="${OUTDIR:-out/harvest-$(date +%H%M%S)}"; mkdir -p "$OUT"
say(){ echo "[$(date +%H:%M:%S)] $*" | tee -a "$OUT/run.log"; }
xp(){ (cd "$XPDIR" && "./$(basename "$XP")" "$@") >>"$OUT/xp.log" 2>&1; }
WABS="C:/Users/Cisco He/Desktop/Git/Patch/Xperia/unattend/$OUT"

rd4(){ # rd4 <addr> <name> — returns hex or "FAIL"
  xp -a "$1" -s 4 -r -O "$WABS/$2.bin"
  if [ -s "$OUT/$2.bin" ]; then xxd -p "$OUT/$2.bin" | tr -d '\n'; else echo FAIL; fi
}
blk(){ # blk <addr> <size> <name>
  xp -a "$1" -s "$2" -r -O "$WABS/$3.bin"
  [ -s "$OUT/$3.bin" ] && xxd "$OUT/$3.bin" | head -12 || echo "READ FAIL"
}

say "== harvest: waiting fastboot =="
for i in $(seq 1 90); do $FB devices 2>/dev/null | grep -q "$SER" && break; sleep 2; done
$FB devices 2>/dev/null | grep -q "$SER" || { say "no fastboot"; exit 1; }

say "patch ABL (-4 -6 -x)"
xp -B -U -s 0xf49880 -x -4 -6
grep -q "patched successfully" "$OUT/xp.log" || { say "patch output missing success — dumping"; tail -8 "$OUT/xp.log"; }
$FB devices 2>/dev/null | grep -q "$SER" || { say "device dropped during patch"; exit 2; }

say "-- restart_reason / stage slots --"
for spec in "0x146bf65c rr" "0x146bf200 imem80" "0x146bf300 imemC0" "0x146bf400 imem100" "0x146bf500 imem140" "0x146bf700 magic700"; do
  set -- $spec
  say "  $2 $1 = $(rd4 "$1" "$2")"
done

say "-- CPU state dump (if magic 0x77667374 at 0x146bf700) --"
blk 0x146bf700 0x80 statedump

say "-- exception record 0x146bf780 (vectors2 catch) --"
blk 0x146bf780 0x80 excvec

say "-- hios return record 0x146bf800 --"
blk 0x146bf800 0x80 hiosret

say "-- ABL log --"
xp -l > "$OUT/abl.log" 2>&1 || true
tail -20 "$OUT/abl.log" | sed 's/^/    /'

say "== done. marks decode: 4Dnn=stage nn, 50nn=panic@nn, E1xx=kernel pre-mark, F0xx=exception slot, E7xx=hios reason, 5501=jumped-no-mark =="
