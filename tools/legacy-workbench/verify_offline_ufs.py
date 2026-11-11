#!/usr/bin/env python3
"""Offline checks of real BootShim execution, firmware inventory and boot AVB.

No ADB, serial, Firehose or device writes. Requires local capstone, unicorn,
pycryptodome and the sibling UniXNU_elden verification script.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import uuid

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_PC

ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT.parents[1] / 'UniXNU_elden'


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


v = module('phase_verify', PORT / 'scripts/verify_phase2_artifact.py')
avb = module('avb_verify', ROOT / 'research/avb_sig_verify.py')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def kernel_blob(path):
    data = path.read_bytes()
    assert data[:8] == b'ANDROID!'
    size = struct.unpack_from('<I', data, 8)[0]
    return gzip.decompress(data[4096:4096 + size])


def execute(shim, fd, in_place):
    target = 0xFF300000
    base = target - len(shim) if in_place else 0xC2400000
    data = shim + fd
    emu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    start = base & ~4095
    end = (base + len(data) + 4095) & ~4095
    emu.mem_map(start, end - start)
    emu.mem_write(base, data)
    if not in_place:
        emu.mem_map(target, len(fd))
    emu.reg_write(UC_ARM64_REG_X0, 0xB0000000)
    emu.reg_write(UC_ARM64_REG_X2, 0xA5A5)
    emu.reg_write(UC_ARM64_REG_X3, 0x5A5A)
    emu.emu_start(base, target, count=1500000)
    assert emu.reg_read(UC_ARM64_REG_PC) == target, 'shim did not reach firmware'
    assert emu.reg_read(UC_ARM64_REG_X0) == 0xB0000000, 'FDT x0 changed'
    assert bytes(emu.mem_read(target, len(fd))) == fd, 'FD copy differs'
    assert emu.reg_read(UC_ARM64_REG_X1) == base + len(shim)
    assert emu.reg_read(UC_ARM64_REG_X2) == base + len(data)
    return {'in_place': in_place, 'fd_copy_sha256': sha(fd),
            'x0': hex(emu.reg_read(UC_ARM64_REG_X0)),
            'x2': hex(emu.reg_read(UC_ARM64_REG_X2)),
            'x3': emu.reg_read(UC_ARM64_REG_X3)}


def inventory(fd):
    fv = v.decompress_fvmain(fd)
    pos = (struct.unpack_from('<H', fv, 48)[0] + 7) & ~7
    result = {}
    while pos + 24 <= len(fv) and fv[pos:pos + 24] != b'\xff' * 24:
        size = int.from_bytes(fv[pos + 20:pos + 23], 'little')
        assert size >= 24 and pos + size <= len(fv), 'bad FFS bounds'
        guid = str(uuid.UUID(bytes_le=fv[pos:pos + 16]))
        sections = []
        label = ''
        cursor = pos + size if fv[pos + 18] in (0x01, 0xF0) else pos + 24
        while cursor + 4 <= pos + size:
            ss = int.from_bytes(fv[cursor:cursor + 3], 'little')
            st = fv[cursor + 3]
            header = 4
            if ss == 0xFFFFFF:
                ss = struct.unpack_from('<I', fv, cursor + 4)[0]
                header = 8
            assert ss >= header and cursor + ss <= pos + size, 'bad FFS section'
            body = fv[cursor + header:cursor + ss]
            if st == 0x15:
                label = body.decode('utf-16-le').rstrip('\0')
            sections.append({'type': st, 'bytes': len(body), 'sha256': sha(body)})
            cursor = (cursor + ss + 3) & ~3
        result[guid] = {'label': label, 'bytes': size, 'sections': sections,
                        'sha256': sha(fv[pos:pos + size])}
        pos = (pos + size + 7) & ~7
    return result


def descriptors(path):
    _, _, blob = avb.vbmeta_offset(str(path))
    h = avb.parse_header(blob)
    aux = blob[256 + h['auth_sz']:]
    pk = aux[h['pk_off']:h['pk_off'] + h['pk_sz']]
    pos, end = h['desc_off'], h['desc_off'] + h['desc_sz']
    props, hashes = {}, []
    data = path.read_bytes()
    while pos < end:
        tag, following = struct.unpack_from('>QQ', aux, pos)
        d = aux[pos:pos + 16 + following]
        assert len(d) == 16 + following
        if tag == 0:
            kl, vl = struct.unpack_from('>QQ', d, 16)
            props[d[32:32 + kl].decode()] = d[33 + kl:33 + kl + vl].decode()
        if tag == 2:
            image_size = struct.unpack_from('>Q', d, 16)[0]
            nl, sl, dl, flags = struct.unpack_from('>4I', d, 56)
            name = d[132:132 + nl].decode()
            salt = d[132 + nl:132 + nl + sl]
            expected = d[132 + nl + sl:132 + nl + sl + dl]
            assert d[24:56].rstrip(b'\0') == b'sha256'
            assert hashlib.sha256(salt + data[:image_size]).digest() == expected
            hashes.append({'name': name, 'image_size': image_size, 'flags': flags,
                           'salt': salt.hex(), 'digest': expected.hex()})
        pos += 16 + following
    assert pos == end
    return {'rollback': h['rollback'], 'location': h['rloc'], 'algorithm': h['alg'],
            'flags': h['flags'], 'pubkey_sha1': hashlib.sha1(pk).hexdigest(),
            'props': props, 'hashes': hashes}


def snapshot_report():
    raw = (ROOT / 'edl_work/entry_snapshot.bin').read_bytes()
    assert len(raw) == 4152
    version, board, status, valid, count, reads, streams, scanned, matches, contexts = struct.unpack_from('<10I', raw)
    assert (version, status, valid) == (1, 0, 7) and count <= 256
    entries = [struct.unpack_from('<QI', raw, 56 + 16 * i) for i in range(count)]
    regs = dict(entries)
    assert len(regs) == count
    cr0, smr, s2cr = [regs[x] for x in (0x15000000, 0x15000800, 0x15000C00)]
    read = (ROOT / 'edl_work/read_report.bin').read_bytes()
    assert len(read) >= 164
    rv, rb, stage, status, release, restored, safe, block_bytes = struct.unpack_from('<8I', read)
    blocks, dma, first, last = struct.unpack_from('<4Q', read, 32)
    assert (rv, stage, status, release, restored, safe) == (1, 9, 0, 0, 1, 1)
    return {'snapshot_sha256': sha(raw), 'snapshot_layout': 'native LE: header 56, entry stride 16',
            'status': 0, 'entries': count, 'streams_scanned': scanned, 'matches': matches,
            'registers': {hex(k): hex(value) for k, value in entries},
            'smmuv2': {'CLIENTPD_bit0': cr0 & 1, 'EXIDENABLE_bit3': (cr0 >> 3) & 1,
                       'SMR_VALID': smr >> 31, 'SMR_MASK': (smr >> 16) & 0x7FFF,
                       'SID': smr & 0xFFFF, 'S2CR_TYPE': (s2cr >> 16) & 3,
                       'CB': s2cr & 255, 'CBAR_TYPE': (regs[0x15001000] >> 16) & 3,
                       'CBA2R_VA64': regs[0x15001800] & 1, 'SCTLR_M': regs[0x15080000] & 1},
            'read_report': {'sha256': sha(read), 'stage': stage, 'handoff_safe': safe,
                            'block_bytes': block_bytes, 'block_count': blocks, 'dma': hex(dma),
                            'first_lba': first, 'last_lba': last,
                            'gpt_unique_guid': str(uuid.UUID(bytes_le=read[64:80]))}}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work', type=Path, default=ROOT / 'edl_work/offline-20261105')
    a = p.parse_args()
    w = a.work
    new = kernel_blob(w / 'ufsprobe-embed56.img')
    old = kernel_blob(ROOT / 'edl_work/probe_signed.img')
    assert old[128:0x400080] == new[128:0x400080]
    assert len(new[0x400080:]) == 56 * 1024 * 1024
    v.verify_embedded_shim(new[:128], 56 * 1024 * 1024)
    try:
        v.verify_fat_type(new[0x400080:0x400080 + (8 << 20)])
    except SystemExit as e:
        fat_rejection = str(e)
    else:
        raise AssertionError('FAT published with only 8 MiB was accepted')
    stale = bytearray(old[:128])
    struct.pack_into('<Q', stale, 16, 60 * 1024 * 1024)
    try:
        v.verify_embedded_shim(stale, 56 * 1024 * 1024)
    except SystemExit as e:
        rejection = str(e)
    else:
        raise AssertionError('header-only stale shim was accepted')
    executions = {}
    for name, blob, size in [('old', old, 8 << 20), ('new', new, 56 << 20)]:
        executions[name] = [execute(blob[:128], blob[128:0x400080], mode)
                            for mode in (False, True)]
        assert all(x['x3'] == size for x in executions[name])
    disasm = '\n'.join(f'{i.address:04x} {i.mnemonic} {i.op_str}' for i in
                        Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN).disasm(new[64:120], 64))
    old_inv = inventory(old[128:0x400080])
    bad_fd = kernel_blob(ROOT / 'edl_work/ufsprobe_v1.img')[128:0x400080]
    new_inv = inventory(bad_fd)
    signed = w / 'ufsprobe-embed56-signed.img'
    meta = descriptors(signed)
    stock = descriptors(ROOT / 'signing/phase5_resign/boot_signed.img')
    probe_template = descriptors(ROOT / 'edl_work/probe_signed.img')
    assert signed.stat().st_size == 100663296
    assert meta['rollback'] == 1738713600 and meta['location'] == 0
    assert meta['pubkey_sha1'] == '2597c218aae470a130f61162feaae70afd97f011'
    assert meta['algorithm'] == 2 and meta['flags'] == 0
    assert meta['props'] == stock['props']
    assert len(meta['hashes']) == 1 and meta['hashes'][0]['name'] == 'boot'
    assert meta['hashes'][0]['salt'] == probe_template['hashes'][0]['salt']
    unsigned = (w / 'ufsprobe-embed56.img').read_bytes()
    assert signed.read_bytes()[:len(unsigned)] == unsigned
    assert avb.verify(str(signed)) is True
    report = {'shim_executions': executions, 'stale_shim_rejected': rejection,
              'truncated_fat_rejected': fat_rejection,
              'new_shim_disassembly': disasm, 'old_fd_sha256': sha(old[128:0x400080]),
              'candidate_fd_unchanged': True, 'old_inventory': old_inv, 'regression_inventory': new_inv,
              'regression_fd_sha256': sha(bad_fd),
              'added_ffs': sorted(new_inv.keys() - old_inv.keys()),
              'removed_ffs': sorted(old_inv.keys() - new_inv.keys()),
              'changed_ffs': sorted(k for k in old_inv.keys() & new_inv.keys()
                                    if old_inv[k]['sha256'] != new_inv[k]['sha256']),
              'avb': meta, 'signed_sha256': sha(signed.read_bytes()),
              'snapshot': snapshot_report(), 'device_tested': False}
    (w / 'independent-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ['stale_shim_rejected', 'old_fd_sha256',
                      'added_ffs', 'removed_ffs', 'signed_sha256', 'device_tested']}, indent=2))


if __name__ == '__main__':
    main()
