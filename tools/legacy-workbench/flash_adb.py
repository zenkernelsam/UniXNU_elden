#!/usr/bin/env python3
"""ADB root flash entry point with mandatory offline AVB/index admission.

Default is an explicit requested write; --dry-run is entirely offline.
Device-side staging is hash checked before dd, then the written prefix is
hash checked. Failures propagate as nonzero exits. This tool never reboots.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import uuid

from avb_guard import check_image
from device_target import ADB, SERIAL

PARTITIONS = tuple(name + suffix for name in ('boot','recovery','vbmeta','vbmeta_system',
                                             'init_boot','vendor_boot','dtbo') for suffix in ('_a','_b'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('partition', choices=PARTITIONS)
    parser.add_argument('--parent-vbmeta', type=Path)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    checked = check_image(args.image, args.partition, args.parent_vbmeta)
    print(json.dumps({'partition': args.partition, 'bytes': checked.bytes, 'sha256': checked.sha256,
                      'index': checked.index, 'location': checked.location, 'admission': 'PASS'}, indent=2))
    if args.dry_run:
        return
    prefix = f'/data/local/tmp/elden-flash-{uuid.uuid4().hex}'
    image, script = prefix + '.img', prefix + '.sh'
    shell = f'''#!/system/bin/sh
set -e
IMAGE={image}
TARGET=/dev/block/bootdevice/by-name/{args.partition}
[ -b "$TARGET" ] || TARGET=/dev/block/by-name/{args.partition}
[ -b "$TARGET" ] || exit 9
SIZE=$(stat -c%s "$IMAGE")
PSIZE=$(blockdev --getsize64 "$TARGET")
[ "$SIZE" -eq {checked.bytes} ] || exit 10
[ "$SIZE" -le "$PSIZE" ] || exit 11
STAGED=$(sha256sum "$IMAGE" | cut -d' ' -f1)
[ "$STAGED" = {checked.sha256} ] || exit 12
dd if="$IMAGE" of="$TARGET" bs=1048576 status=none
sync
READBACK=$(dd if="$TARGET" bs=4096 count={checked.bytes // 4096} status=none | sha256sum | cut -d' ' -f1)
[ "$READBACK" = {checked.sha256} ] || exit 13
echo "FLASH OK: {args.partition} hash match"
'''

    def adb(*arguments, check=True):
        return subprocess.run([str(ADB), '-s', SERIAL, *map(str, arguments)], check=check)

    uid = subprocess.run([str(ADB), '-s', SERIAL, 'shell', 'id', '-u'],
                         capture_output=True, text=True, check=True).stdout.strip()
    execute = f'sh {script}' if uid == '0' else f"su -c 'sh {script}'"
    with tempfile.TemporaryDirectory(prefix='elden-adb-') as directory:
        local = Path(directory) / 'flash.sh'
        local.write_text(shell, encoding='utf-8', newline='\n')
        try:
            adb('push', checked.path, image)
            adb('push', local, script)
            adb('shell', execute)
        finally:
            adb('shell', f'rm -f {image} {script}', check=False)


if __name__ == '__main__':
    main()
