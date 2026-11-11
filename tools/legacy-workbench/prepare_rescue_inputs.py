"""Extract admitted stock EFI kernel + vendor/TWRP ramdisks for a UEFI prototype.

No device reads/writes and no promise of recovery boot/decryption. This avoids
passing a kernel-less Android recovery container to UEFI LoadImage.
"""
import hashlib
import argparse
import json
from pathlib import Path
import struct

from avb_guard import check_image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True, help='local-only device serial for recovery USB enumeration')
    args = parser.parse_args()
    assert args.serial.isalnum(), 'serial contains invalid command-line characters'
    boot = ROOT / 'signing/phase5_resign/boot_signed.img'
    recovery = ROOT / 'edl_work/twrp_TB322FC_signed.img'
    vendor = ROOT / 'Backup/B_edl_stock/lun4/vendor_boot_a.bin'
    parent = ROOT / 'signing/phase5_resign/vbmeta_signed.img'
    check_image(boot, 'boot_a')
    check_image(recovery, 'recovery_a')
    check_image(vendor, 'vendor_boot_a', parent)
    out = ROOT / 'edl_work/rescue-20261106'
    out.mkdir(parents=True, exist_ok=True)
    b, r, v = boot.read_bytes(), recovery.read_bytes(), vendor.read_bytes()
    ks = struct.unpack_from('<I', b, 8)[0]
    kernel = b[4096:4096+ks]
    assert kernel[:2] == b'MZ'
    pe = struct.unpack_from('<I', kernel, 60)[0]
    assert kernel[pe:pe+4] == b'PE\0\0'
    assert struct.unpack_from('<H', kernel, pe+4)[0] == 0xAA64
    assert struct.unpack_from('<H', kernel, pe+24+68)[0] == 10  # EFI application
    assert struct.unpack_from('<I', r, 8)[0] == 0
    rd = r[4096:4096+struct.unpack_from('<I', r, 12)[0]]
    assert rd[:4] == bytes.fromhex('02214c18')
    assert v[:8] == b'VNDRBOOT' and struct.unpack_from('<I', v, 8)[0] == 4
    vrsize = struct.unpack_from('<I', v, 24)[0]
    pages = struct.unpack_from('<I', v, 12)[0]
    assert pages == 4096
    vrd = v[pages:pages+vrsize]
    assert vrd[:4] == bytes.fromhex('02214c18')
    dt = struct.unpack_from('<I', v, 2100)[0]
    tablebytes, entries, stride, cb = struct.unpack_from('<4I', v, 2112)
    table = pages+((vrsize+pages-1)//pages)*pages+((dt+pages-1)//pages)*pages
    assert entries == 1 and stride == 108
    size, offset, kind = struct.unpack_from('<III', v, table)
    assert (size, offset, kind) == (vrsize, 0, 1)  # platform ramdisk
    bcstart = table+((tablebytes+pages-1)//pages)*pages
    config = v[bcstart:bcstart+cb].rstrip(b'\0')
    config += b'androidboot.mode=recovery\nandroidboot.force_normal_boot=0\n'
    config += b'\0'
    initrd = vrd+rd
    config += bytes((-len(initrd)-len(config)) % 4)
    initrd += config+struct.pack('<II', len(config), sum(config)&0xffffffff)+b'#BOOTCONFIG\n'
    (out/'Image').write_bytes(kernel)
    (out/'recovery-initrd.img').write_bytes(initrd)
    cmd = v[28:2076].split(b'\0')[0].decode('ascii')
    cmd += ' rdinit=/init androidboot.mode=recovery androidboot.force_normal_boot=0'
    cmd += ' androidboot.serialno=' + args.serial
    cmd += ' androidboot.boot_devices=soc/1d84000.ufshc'
    report = {'status': 'offline UEFI/Linux rescue prototype only; device boot unverified',
              'kernel': {'bytes':len(kernel),'sha256':hashlib.sha256(kernel).hexdigest(),
                         'machine':'AARCH64','subsystem':'EFI_APPLICATION'},
              'initrd': {'bytes':len(initrd),'sha256':hashlib.sha256(initrd).hexdigest(),
                         'vendor_bytes':len(vrd),'twrp_bytes':len(rd),'bootconfig_bytes':len(config)},
              'linux_args':cmd,'inputs':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (boot,recovery,vendor,parent)}}
    (out/'inputs.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
