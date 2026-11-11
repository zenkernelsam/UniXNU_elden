#!/bin/sh
# Builds a local diagnostic image; no device access.
set -eu
cd /Users/ciscohe/Desktop/UniXNU_elden
tag="${1:-vmcheck}"
point="${2:-0xfffffff0079bd910}"
case "$tag" in *[!a-z0-9_-]*|'') exit 2;; esac
case "$point" in 0xfffffff0*) ;; *) exit 2;; esac
dest="out/fix-wfe-counter-${tag}-20261106"
test ! -e "$dest"
export PATH="$PWD/.cache/mtools/bin:$PATH"
python3 - <<'PY'
import sys,struct,hashlib
from pathlib import Path
sys.path.insert(0, 'scripts')
import make_entry_probe_kernel as p
b=Path('third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_rootcheck/kernel/kernel.macho').read_bytes()
assert hashlib.sha256(b).hexdigest()=='07d4df1d49e0dd64acab02e84a74d452c7034acd92e5a44b0ce37d67f53e4b15'
_,offset=p.locate(b,0xfffffff0073f2f50,4,executable=True)
assert struct.unpack_from('<I',b,offset)[0]==0xf9402529
encoded=p.assemble(Path('/usr/bin/clang'),p.STUB_HEADER+'  mrs x9, cntfrq_el0\n')
assert encoded==struct.pack('<I',0xd53be009)
print('verified LDR frequency -> architectural CNTFRQ_EL0:',encoded.hex())
PY
python3 scripts/make_entry_probe_kernel.py \
 --kernel third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_rootcheck/kernel/kernel.macho \
 --adt out/offline-20261105/kernel.macho.adt \
 --expect-kernel-bytes 52023296 \
 --expect-kernel-sha256 07d4df1d49e0dd64acab02e84a74d452c7034acd92e5a44b0ce37d67f53e4b15 \
 --poke 0xfffffff004aaecfc=0x17ffffd4 --poke 0xfffffff0073f2f50=0xd53be009 \
 --reset-va "$point" --reset-delay 2 --clang /usr/bin/clang --output-dir "$dest"
cp out/rootcheck-entry-20261106/payload.img "$dest/payload.img"
mcopy -o -i "$dest/payload.img" "$dest/probe/kernel.macho" ::EFI/UniXNU/Payloads/kernel.macho
python3 scripts/build_phase2_image.py --kind boot \
 --fd out/phase3/old-fd-verified.bin --payload "$dest/payload.img" \
 --output "$dest/ufsprobe-bisect.img" --embed-payload --work "$dest" --clang /usr/bin/clang
shasum -a 256 "$dest/ufsprobe-bisect.img" "$dest/probe/kernel.macho"
