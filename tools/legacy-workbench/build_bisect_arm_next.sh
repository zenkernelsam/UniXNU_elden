#!/bin/sh
# Local-image build on the Mac; no device access.
set -eu
cd /Users/ciscohe/Desktop/UniXNU_elden
tag="$1"
point="$2"
adt="${3:-out/offline-20261105/kernel.macho.adt}"
adt_sha="${4:-b0e7c8937e4aec2e8621fc055f08d43dee08f640e62e9898c9afd786bab303e3}"
case "$tag" in *[!a-z0-9_-]*|'') exit 2;; esac
case "$point" in 0xfffffff0*) ;; *) exit 2;; esac
dest="out/bisect-${tag}-20261106"
test ! -e "$dest"
export PATH="$PWD/.cache/mtools/bin:$PATH"
python3 - "$adt" "$adt_sha" <<'PY'
from pathlib import Path
import hashlib,sys
checks = {
 sys.argv[1]: sys.argv[2],
 'out/phase3/old-fd-verified.bin': 'e50e1d6e2be51367225d76d4e97b90d51ba7812193d99fd4d4d7dd9fdc2ee534',
}
for name, expected in checks.items():
 actual = hashlib.sha256(Path(name).read_bytes()).hexdigest()
 if actual != expected: raise SystemExit('Unexpected input: '+name)
 print('verified', name, actual)
PY
python3 scripts/make_entry_probe_kernel.py \
 --kernel third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_rootcheck/kernel/kernel.macho \
 --adt "$adt" \
 --expect-kernel-bytes 52023296 \
 --expect-kernel-sha256 07d4df1d49e0dd64acab02e84a74d452c7034acd92e5a44b0ce37d67f53e4b15 \
 --poke 0xfffffff004aaecfc=0x17ffffd4 --reset-va "$point" --reset-delay 2 \
 --clang /usr/bin/clang --output-dir "$dest"
cp out/rootcheck-entry-20261106/payload.img "$dest/payload.img"
mcopy -o -i "$dest/payload.img" "$dest/probe/kernel.macho" ::EFI/UniXNU/Payloads/kernel.macho
mcopy -o -i "$dest/payload.img" "$dest/probe/kernel.macho.adt" ::EFI/UniXNU/Payloads/kernel.macho.adt
python3 scripts/build_phase2_image.py --kind boot \
 --fd out/phase3/old-fd-verified.bin --payload "$dest/payload.img" \
 --output "$dest/ufsprobe-bisect.img" --embed-payload --work "$dest" --clang /usr/bin/clang
shasum -a 256 "$dest/ufsprobe-bisect.img" "$dest/probe/kernel.macho"
