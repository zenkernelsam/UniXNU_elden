"""Mutation checks using a user-supplied accepted FD; never writes that input."""
from pathlib import Path
import argparse
import json
import struct
from firmware_contract import verify_firmware


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('firmware', type=Path)
    args = parser.parse_args()
    original = args.firmware.read_bytes()
    verify_firmware(original)
    offset = (struct.unpack_from('<H', original, 48)[0]+7)&~7
    section = offset+24
    while original[section+3] != 0x12:
        section = (section+int.from_bytes(original[section:section+3], 'little')+3)&~3
    te = section+4
    guid = original.find(b'RSDS', te, te+int.from_bytes(original[section:section+3], 'little'))+4
    if guid < te+4:
        raise ValueError('Expected SEC CodeView record missing')
    changed = bytearray(original)
    changed[guid] ^= 1
    verify_firmware(changed)
    # The TE raw section pointer uses the stripped PE header coordinates.
    stripped = struct.unpack_from('<H', original, te+6)[0]-40
    first_raw = struct.unpack_from('<I', original, te+40+20)[0]-stripped
    cases = {'sec_code': te+first_raw, 'fv_header': 0,
             'ffs_header_checksum': offset+16, 'erased_tail': len(original)-1}
    for name, at in cases.items():
        damaged = bytearray(original)
        damaged[at] ^= 1
        try:
            verify_firmware(damaged)
        except (ValueError, struct.error):
            pass
        else:
            raise AssertionError('Mutation was accepted: '+name)
    print(json.dumps({'baseline_accepted': True, 'codeview_guid_only_accepted': True,
                      'rejected_mutations': list(cases), 'device_operation': False}, indent=2))


if __name__ == '__main__':
    main()
