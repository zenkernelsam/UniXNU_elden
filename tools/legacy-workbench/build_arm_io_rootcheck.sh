#!/bin/sh
set -eu
cd /Users/ciscohe/Desktop/UniXNU_elden
dest=out/fix-arm-io-rootcheck-20261106
test ! -e "$dest"
mkdir "$dest"
export PATH="$PWD/.cache/mtools/bin:$PATH"
cp out/rootcheck-entry-20261106/payload.img "$dest/payload.img"
mcopy -o -i "$dest/payload.img" out/arm-io-fix-20261106/kernel.macho.adt ::EFI/UniXNU/Payloads/kernel.macho.adt
python3 scripts/build_phase2_image.py --kind boot \
 --fd out/phase3/old-fd-verified.bin --payload "$dest/payload.img" \
 --output "$dest/boot.img" --embed-payload --work "$dest" --clang /usr/bin/clang
shasum -a 256 "$dest/boot.img"
