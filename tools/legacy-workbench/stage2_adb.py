"""Owner-operated ADB stage2 slot update, with no device access by default.

--inspect reads the specific LUN4 ARMADA_BACKUP partition. --apply writes only
its inactive slot, preserving the explicitly named verified generation. An
initial install additionally requires a full matching partition backup.
This command never changes GPT, signed boot/recovery, slots, or reboots.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import uuid

from device_target import ADB, SERIAL
from stage2_store import MIB, checked_fat, inventory, transaction


class AdbStore:
    def __init__(self):
        self.prefix = [str(ADB), '-s', SERIAL]
        self.root = self.adb('shell', 'id -u').strip() == '0'
        self.staging = ('/tmp/' if self.root else '/data/local/tmp/') + 'elden-stage2-' + uuid.uuid4().hex
        self.target = None
        # Identity, LUN, geometry, mounts and holders are checked before any
        # transfer or write. The name alone is not enough to authorize a node.
        result = self.shell('''set -e
TARGET=/dev/block/bootdevice/by-name/ARMADA_BACKUP
[ -b "$TARGET" ] || TARGET=/dev/block/by-name/ARMADA_BACKUP
[ -b "$TARGET" ] || exit 31
TARGET=$(readlink -f "$TARGET")
NODE=$(basename "$TARGET")
PARENT=$(basename "$(dirname "$(readlink -f /sys/class/block/$NODE)")")
SCSI=$(basename "$(readlink -f /sys/class/block/$PARENT/device)")
[ "${SCSI##*:}" = 4 ] || exit 32
[ "$(cat /sys/class/block/$PARENT/queue/logical_block_size)" = 4096 ] || exit 33
[ -f /sys/class/block/$NODE/partition ] || exit 34
DEV=$(cat /sys/class/block/$NODE/dev)
if awk -v device="$DEV" '$3 == device {found=1} END {exit found?0:1}' /proc/self/mountinfo; then exit 35; fi
[ -z "$(ls -A /sys/class/block/$NODE/holders)" ] || exit 36
printf '%s\\n' "$TARGET" "$PARENT" "$SCSI"
blockdev --getsize64 "$TARGET"
''').strip().splitlines()
        if (len(result) != 4 or not re.fullmatch(r'/dev/block/sd[a-z]+[0-9]+', result[0]) or
                not re.fullmatch(r'sd[a-z]+', result[1]) or
                not re.fullmatch(r'\d+:\d+:\d+:4', result[2]) or not result[3].isdigit()):
            raise RuntimeError('unexpected partition identity response')
        self.target, self.parent, self.scsi = result[:3]
        self.size = int(result[3])
        if self.size < 128*MIB or self.size % 4096:
            raise RuntimeError('unexpected partition capacity')

    def adb(self, *arguments, timeout=300):
        result = subprocess.run([*self.prefix, *map(str, arguments)], capture_output=True,
                                text=True, check=True, timeout=timeout)
        return result.stdout

    def shell(self, script):
        command = 'sh -c ' + shlex.quote(script)
        if not self.root:
            command = 'su -c ' + shlex.quote(command)
        return self.adb('shell', command)

    def bounds(self, offset, size):
        if offset < 0 or size <= 0 or offset % 512 or size % 512 or offset+size > self.size:
            raise ValueError('unaligned request or range outside admitted partition')

    def read(self, offset, size):
        self.bounds(offset, size)
        if size > 4096:
            raise ValueError('ADB metadata read is bounded to one header')
        output = self.shell(f'dd if={shlex.quote(self.target)} bs=512 skip={offset//512} count={size//512} status=none | base64')
        data = base64.b64decode(''.join(output.split()), validate=True)
        if len(data) != size:
            raise IOError('short header read')
        return data

    def digest(self, offset, size):
        self.bounds(offset, size)
        output = self.shell(f'dd if={shlex.quote(self.target)} bs=512 skip={offset//512} count={size//512} status=none | sha256sum')
        match = re.fullmatch(r'([0-9a-fA-F]{64})\s+-\s*', output)
        if not match:
            raise IOError('ambiguous partition digest')
        return bytes.fromhex(match[1])

    def write(self, offset, data):
        self.bounds(offset, len(data))
        if offset % 4096:
            raise ValueError('write must begin at a 4KiB boundary')
        digest = hashlib.sha256(data).hexdigest()
        with tempfile.TemporaryDirectory(prefix='elden-stage2-') as directory:
            file = Path(directory)/'part.bin'
            file.write_bytes(data)
            try:
                self.adb('push', file, self.staging)
                self.shell(f'''set -e
STAGED={shlex.quote(self.staging)}
[ "$(stat -c%s "$STAGED")" = {len(data)} ] || exit 41
[ "$(sha256sum "$STAGED" | cut -d' ' -f1)" = {digest} ] || exit 42
dd if="$STAGED" of={shlex.quote(self.target)} bs=4096 seek={offset//4096} conv=notrunc status=none
''')
            finally:
                self.adb('shell', 'rm -f '+shlex.quote(self.staging))

    def write_payload(self, offset, data):
        # One USB push and one dd for the whole bundle, not one ADB session
        # per 4KiB storage block. The shared transaction verifies it afterwards.
        self.write(offset, data)

    def sync(self):
        self.shell('sync')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    action = p.add_mutually_exclusive_group()
    action.add_argument('--inspect', action='store_true', help='read-only device admission and slot digest check')
    action.add_argument('--apply', action='store_true', help='explicitly write inactive slot on the device')
    p.add_argument('--bundle', type=Path)
    p.add_argument('--keep-generation', type=int)
    p.add_argument('--initialize', action='store_true')
    p.add_argument('--backup', type=Path, help='mandatory complete current partition backup for initialization')
    p.add_argument('--report', type=Path)
    a = p.parse_args()
    data = checked_fat(a.bundle) if a.bundle else None
    if a.apply and data is None:
        p.error('--apply requires --bundle')
    if a.initialize and (a.keep_generation is not None or a.backup is None):
        p.error('--initialize requires --backup and no keep generation')
    if a.apply and not a.initialize and a.keep_generation is None:
        p.error('--apply requires --keep-generation or --initialize')
    if not (a.inspect or a.apply):
        print(json.dumps(dict(device_access=False, mode='offline input check',
            bytes=len(data) if data else None, sha256=hashlib.sha256(data).hexdigest() if data else None,
            next_step='Owner uses --inspect, then explicitly retains a verified generation with --apply.'), indent=2))
        return
    store = AdbStore()
    if a.initialize:
        if a.backup.stat().st_size != store.size:
            raise ValueError('backup size does not cover the entire partition')
        with a.backup.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').digest()
        if store.digest(0, store.size) != digest:
            raise ValueError('backup does not match the current complete partition')
    if a.apply:
        result = transaction(store, data, a.keep_generation, a.initialize, commit=True)
    else:
        _, entries, valid = inventory(store)
        result = dict(bytes=store.size, target=store.target, lun=4,
            slots=[dict(slot=i.slot, generation=i.generation, bytes=i.size, sha256=i.digest.hex(),
                        payload_verified=i in valid) for i in entries if i])
    text = json.dumps(result, indent=2)+'\n'
    if a.report:
        a.report.write_text(text, encoding='utf-8')
    print(text, end='')


if __name__ == '__main__':
    main()
