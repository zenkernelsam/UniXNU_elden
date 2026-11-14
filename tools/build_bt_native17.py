"""Build/sign the offline 21H461 Bluetooth routing component; never install it."""
from pathlib import Path
import argparse, hashlib, json, plistlib, re, struct, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'drivers/EldenBluetooth/userland'))
from Native17 import Image, ORIGINAL_SHA256, ORIGINAL_UUID

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def signature(path):
    subprocess.run(['codesign', '--verify', '--strict', str(path)], check=True)
    result = subprocess.run(['codesign', '--display', '--verbose=4', str(path)],
                            check=True, capture_output=True, text=True).stderr
    identifier = re.search(r'^Identifier=(.+)$', result, re.M)
    digest = re.search(r'^CDHash=([0-9a-f]{40})$', result, re.M)
    if not identifier or not digest:
        raise ValueError('Missing signature identity')
    return {'identifier': identifier[1], 'cdhash': digest[1]}

def entitlements(path):
    result = subprocess.run(['codesign', '--display', '--entitlements', ':-', str(path)],
                            check=True, capture_output=True)
    return plistlib.loads(result.stdout)

def main():
    if sys.flags.optimize:
        raise ValueError('Assertions required')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Fresh output required')
    image = Image(args.image).routing()
    original_signature = signature(args.image)
    original_entitlements = entitlements(args.image)
    args.output.mkdir(parents=True)
    target = args.output / 'bluetoothd.routing-only'
    target.write_bytes(image.data)
    target.chmod(0o755)
    subprocess.run(['codesign', '--force', '--sign', '-', '--pagesize', '4096',
                    '--preserve-metadata=identifier,entitlements,flags', str(target)], check=True)
    current_signature = signature(target)
    if current_signature['identifier'] != original_signature['identifier'] or entitlements(target) != original_entitlements:
        raise ValueError('Original identity or entitlements changed')
    final = target.read_bytes()
    if struct.unpack_from('<4I', final) != (0xfeedfacf, 0x100000c, 0, 2):
        raise ValueError('Architecture changed')
    # codesign may update LINKEDIT and LC_CODE_SIGNATURE only. Code, constants,
    # data mappings, UUID and our previously unused executable padding must stay.
    allowed = []
    at = 32
    for _ in range(struct.unpack_from('<I', final, 16)[0]):
        command, size = struct.unpack_from('<II', final, at)
        if command == 0x19 and final[at + 8:at + 24].rstrip(b'\0') == b'__LINKEDIT':
            allowed.append((at + 32, at + 40)); allowed.append((at + 48, at + 56))
        elif command == 0x1d:
            allowed.append((at + 8, at + 16))
        at += size
    limit = next(off for name, _, _, off, _, _ in image.segments if name == '__LINKEDIT')
    normalized = bytearray(final[:limit])
    for first, last in allowed:
        normalized[first:last] = image.data[first:last]
    if normalized != image.data[:limit]:
        raise ValueError('Signing changed non-signature image bytes')
    report = {'input_sha256': ORIGINAL_SHA256, 'input_uuid': ORIGINAL_UUID,
              'input_signature': original_signature, 'signature': current_signature,
              'sha256': sha(target), 'bytes': target.stat().st_size,
              'original_entitlements_preserved': True, 'entitlement_count': len(original_entitlements),
              'all_pre_linkedit_bytes_preserved_except_signature_commands': True,
              'patch': image.report(), 'sources': {str(path.relative_to(ROOT)): sha(path) for path in
                  (ROOT / 'drivers/EldenBluetooth/userland/Native17.py', Path(__file__).resolve())},
              'installed': False, 'new_boot_image': False, 'hardware_verified': False,
              'limits': ['Routing component only; F3 remains original and must be adapted at integration.',
                         'Capability publication, optional HCI commands, keys and full daemon initialization remain unverified.',
                         'Current v117 runtime/trust/text guard reject this different daemon identity; do not drop it into that payload.']}
    (args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('sha256', 'bytes', 'signature', 'entitlement_count', 'installed')}, indent=2))

if __name__ == '__main__':
    main()
