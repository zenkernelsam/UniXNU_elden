"""Strict structural firmware fingerprint; only CodeView GUIDs are variable.

Compression output/length and enclosing FFS checksums are represented by their
decompressed content. Every executable byte, dependency, GUID, file order,
section kind, padding rule and FV header remains part of the contract.
"""
import hashlib
import json
import lzma
import struct
import uuid
from pathlib import Path

LZMA = uuid.UUID('ee4e5898-3914-4259-9d6e-dc7bd79403cf').bytes_le
PROFILE = Path(__file__).resolve().parents[1] / 'profiles/v91/firmware-build/contract.json'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pe_contract(data):
    blob = bytearray(data)
    if blob[:2] != b'MZ':
        raise ValueError('Expected PE/COFF image')
    pe = struct.unpack_from('<I', blob, 60)[0]
    if blob[pe:pe+4] != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    machine, count = struct.unpack_from('<HH', blob, pe+4)
    size = struct.unpack_from('<H', blob, pe+20)[0]
    if machine != 0xaa64 or struct.unpack_from('<H', blob, pe+24)[0] != 0x20b:
        raise ValueError('Only ARM64 PE32+ is qualified')
    regions = []
    for offset in range(pe+24+size, pe+24+size+40*count, 40):
        virtual_size, virtual, raw_size, raw = struct.unpack_from('<4I', blob, offset+8)
        if raw + raw_size > len(blob):
            raise ValueError('PE region exceeds image')
        regions.append((virtual, raw, raw_size))

    def raw_at(rva, length):
        for virtual, raw, raw_size in regions:
            if virtual <= rva and rva + length <= virtual + raw_size:
                return raw + rva - virtual
        raise ValueError('PE directory outside sections')

    masks = []
    debug_rva, debug_size = struct.unpack_from('<II', blob, pe+24+112+6*8)
    if debug_rva:
        if debug_size % 28:
            raise ValueError('Invalid debug directory size')
        base = raw_at(debug_rva, debug_size)
        for offset in range(base, base+debug_size, 28):
            kind, length, rva, raw = struct.unpack_from('<4I', blob, offset+12)
            if kind == 2 and blob[raw:raw+4] == b'RSDS':
                if length < 25 or raw_at(rva, length) != raw or blob[raw:raw+4] != b'RSDS':
                    raise ValueError('Unexpected CodeView directory')
                masks.append([raw+4, 16])
                blob[raw+4:raw+20] = bytes(16)
    return {'bytes': len(blob), 'codeview_guid_ranges': masks, 'sha256': digest(blob)}


def te_contract(data):
    blob = bytearray(data)
    if blob[:4] != b'VZ\x64\xaa':
        raise ValueError('Expected ARM64 TE image')
    count = blob[4]
    delta = struct.unpack_from('<H', blob, 6)[0] - 40
    regions = []
    for offset in range(40, 40 + 40*count, 40):
        virtual_size, virtual, raw_size, raw = struct.unpack_from('<4I', blob, offset+8)
        regions.append((virtual, raw-delta, raw_size))

    def raw_at(rva, length):
        for virtual, raw, raw_size in regions:
            if virtual <= rva and rva+length <= virtual+raw_size and 0 <= raw and raw+raw_size <= len(blob):
                return raw+rva-virtual
        raise ValueError('TE directory outside sections')

    debug_rva, debug_size = struct.unpack_from('<II', blob, 32)
    masks = []
    if debug_rva:
        if debug_size % 28:
            raise ValueError('Invalid TE debug directory')
        base = raw_at(debug_rva, debug_size)
        for offset in range(base, base+debug_size, 28):
            kind, length, rva, pointer = struct.unpack_from('<4I', blob, offset+12)
            raw = raw_at(rva, length)
            if kind == 2 and blob[raw:raw+4] == b'RSDS':
                if length < 25 or pointer-delta != raw:
                    raise ValueError('Unexpected TE CodeView range')
                masks.append([raw+4, 16])
                blob[raw+4:raw+20] = bytes(16)
    return {'bytes': len(blob), 'codeview_guid_ranges': masks, 'sha256': digest(blob)}


def section_stream(data):
    offset = 0
    records = []
    while offset < len(data):
        if offset + 4 > len(data):
            raise ValueError('Truncated section')
        size = int.from_bytes(data[offset:offset+3], 'little')
        kind = data[offset+3]
        header = 4
        if size == 0xffffff:
            size = struct.unpack_from('<I', data, offset+4)[0]
            header = 8
        if size < header or offset + size > len(data):
            raise ValueError('Section bounds invalid')
        body = data[offset+header:offset+size]
        record = {'kind': kind, 'header_bytes': header}
        if kind == 0x10:
            record['pe'] = pe_contract(body)
        elif kind == 0x12:
            record['te'] = te_contract(body)
        elif kind == 0x17:
            record['volume'] = volume_contract(body)
        elif kind == 0x02 and body[:16] == LZMA:
            start, flags = struct.unpack_from('<HH', body, 16)
            if start < header + 20 or start > size:
                raise ValueError('Invalid guided section header')
            record['guided_header'] = body[:start-header].hex()
            decoder = lzma.LZMADecompressor()
            unpacked = decoder.decompress(body[start-header:], max_length=32*1024*1024)
            if not decoder.eof or decoder.unused_data:
                raise ValueError('Truncated, excessive or trailing compressed firmware data')
            record['contents'] = section_stream(unpacked)
        else:
            record.update(bytes=len(body), sha256=digest(body))
        end = offset + size
        next_offset = min((end+3)&~3, len(data))
        record['padding'] = data[end:next_offset].hex()
        records.append(record)
        offset = next_offset
    return records


def volume_contract(data):
    if len(data) < 56 or data[40:44] != b'_FVH' or struct.unpack_from('<Q', data, 32)[0] != len(data):
        raise ValueError('Invalid firmware volume')
    header_size = struct.unpack_from('<H', data, 48)[0]
    if header_size < 56 or header_size > len(data):
        raise ValueError('Invalid volume header')
    out = {'bytes': len(data), 'header_sha256': digest(data[:header_size]), 'files': []}
    offset = (header_size+7)&~7
    out['header_padding'] = data[header_size:offset].hex()
    while offset+24 <= len(data) and data[offset:offset+24] != b'\xff'*24:
        size = int.from_bytes(data[offset+20:offset+23], 'little')
        header = 24
        if size == 0xffffff:
            raise ValueError('Large FFS is outside the accepted profile')
        if size < header or offset+size > len(data):
            raise ValueError('Invalid FFS bounds')
        file = data[offset:offset+size]
        # State and file checksum are excluded from the header checksum.
        if (sum(file[:24])-file[17]-file[23]) & 255:
            raise ValueError('Invalid FFS header checksum')
        if file[19] & 0x40:
            if (sum(file[24:])+file[17]) & 255:
                raise ValueError('Invalid FFS data checksum')
        elif file[17] != 0xaa:
            raise ValueError('Unexpected fixed FFS data checksum')
        record = {'guid': str(uuid.UUID(bytes_le=file[:16])), 'type': file[18],
                  'attributes': file[19], 'state': file[23]}
        if file[18] in (1, 0xf0):
            record.update(bytes=len(file[24:]), sha256=digest(file[24:]))
        else:
            try:
                record['sections'] = section_stream(file[24:])
            except ValueError as error:
                raise ValueError(record['guid'] + ': ' + str(error)) from error
        end = offset + size
        next_offset = (end+7)&~7
        record['padding'] = data[end:next_offset].hex()
        if file[18] == 11:
            if any(x != 255 for x in data[end:next_offset]):
                raise ValueError('Invalid compressed-volume alignment padding')
            record['padding'] = 'erased-alignment'
        out['files'].append(record)
        offset = next_offset
    # The length can change when compressed GUIDs have different entropy.
    if any(x != 255 for x in data[offset:]):
        raise ValueError('Non-erased data after last FFS')
    return out


def verify_firmware(path):
    data = bytes(path) if isinstance(path, (bytes, bytearray)) else Path(path).read_bytes()
    expected = json.loads(PROFILE.read_text())
    actual = volume_contract(data)
    if actual != expected['structure']:
        raise ValueError('Firmware differs outside the exact debug/compression contract')
    return {'sha256': digest(data), 'contract_sha256': digest(PROFILE.read_bytes()),
            'all_executable_bytes_equal': True, 'hardware_tested': False}


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('firmware', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit('Report path must be new')
    result = verify_firmware(args.firmware)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
