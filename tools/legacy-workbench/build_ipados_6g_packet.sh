#!/bin/sh
set -eu
cd /Users/ciscohe/Desktop/UniXNU_elden
dest=out/ipados-6g-entry-20261107
mkdir -p "$dest"
export PATH="$PWD/.cache/mtools/bin:$PATH"
cp out/ipados-console-entry-20261107/payload.img "$dest/payload.img"
mcopy -o -i "$dest/payload.img" "$dest/kernel.macho.adt" ::EFI/UniXNU/Payloads/kernel.macho.adt
.venv/bin/python scripts/darwinboot_preflight.py --kernel out/ipados-console-entry-20261107/probe/kernel.macho --adt "$dest/kernel.macho.adt"
.venv/bin/python scripts/build_phase2_image.py --kind boot --embed-payload \
 --fd out/phase3/old-fd-verified.bin --payload "$dest/payload.img" --output "$dest/boot.img" \
 --work "$dest" --clang /usr/bin/clang
.venv/bin/python scripts/verify_phase2_artifact.py --kind boot --image "$dest/boot.img" \
 --fd out/phase3/old-fd-verified.bin --shim "$dest/shim-boot-embed.bin" --embed-payload --payload "$dest/payload.img" \
 --expect-kernel-bytes 52019304 --expect-kernel-sha256 95e8869cf7c71372f1b1684671800ef1507bd0919e79774f2aa2eee08dd4ad77 \
 --xnu-title 'ELDEN IPADOS ROOT CHECK - LUN0' --report "$dest/verify.json"
