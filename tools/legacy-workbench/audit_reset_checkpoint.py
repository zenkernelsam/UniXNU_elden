"""Offline audit of one 48-byte reset checkpoint relative to the fixed R1 kernel."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
import sys

from audit_boot_candidate import Fat
from avb_guard import check_image
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parents[1] / 'UniXNU_elden/scripts'))
from make_entry_probe_kernel import locate


def unpack(path):
    b = path.read_bytes()
    return gzip.decompress(b[4096:4096+struct.unpack_from('<I', b, 8)[0]])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('image', type=Path)
    ap.add_argument('va', type=lambda s: int(s, 0))
    ap.add_argument('kernel_sha256')
    a = ap.parse_args()
    checked = check_image(a.image, 'boot_a')
    reference = ROOT/'edl_work/rootcheck-entry-20261106/boot-signed.img'
    if hashlib.sha256(reference.read_bytes()).hexdigest() != '345e50526b6c78806470203d7e9f9b652b7ef124ec4e7b59fc2214179b4afb47':
        raise ValueError('R1 reference changed')
    raw, old = unpack(a.image), unpack(reference)
    assert len(raw) == len(old) == 0x3c00080 and raw[:0x400080] == old[:0x400080]
    fat, original = Fat(raw[0x400080:]), Fat(old[0x400080:])
    kpath = '/EFI/UniXNU/Payloads/kernel.macho'
    kernel, baseline = fat.read(kpath), original.read(kpath)
    assert hashlib.sha256(kernel).hexdigest() == a.kernel_sha256
    assert fat.read(kpath+'.adt') == original.read(kpath+'.adt')
    assert fat.read('/EFI/UniXNU/unixnu.toml') == original.read('/EFI/UniXNU/unixnu.toml')
    _, offset = locate(kernel, a.va, 48, executable=True)
    assert len(kernel) == len(baseline) and kernel[:offset] == baseline[:offset] and kernel[offset+48:] == baseline[offset+48:]
    ins = list(Cs(CS_ARCH_ARM64, CS_MODE_ARM).disasm(kernel[offset:offset+48], a.va))
    assert [i.mnemonic for i in ins] == ['mrs','mov','mul','mrs','add','mrs','cmp','b.lo','mov','movk','smc','b']
    assert ins[1].op_str == 'x2, #2' and ins[8].op_str == 'x0, #9' and ins[9].op_str == 'x0, #0x8400, lsl #16'
    report = vars(checked) | {'path': str(checked.path), 'reset_va': hex(a.va),
        'kernel_sha256': a.kernel_sha256, 'only_kernel_range_changed': [offset, offset+48],
        'shim_fd_adt_config_equal_R1': True, 'device_written_by_this_audit': False}
    a.image.with_name('admission.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
