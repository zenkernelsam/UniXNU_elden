# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Version-bound 21H461 model/type-zero routing, following QcaBluetooth's method.

This is an offline component. It does not sign, install, report HCI capabilities,
or claim that the native daemon can yet initialize/scan/pair on hardware.
"""
from pathlib import Path
import hashlib
import struct
import uuid

ORIGINAL_SHA256 = '3f1e4493ea6a76976625f7af58a8f330c0c90d82d9020b11110b4f3f5b758df4'
ORIGINAL_UUID = '0f836381-d07b-36e4-9dbb-8a44782b9c14'
BASE = 0x100000000
FACTORY = BASE + 0x31ad30
GETTER = BASE + 0x4681c
MODEL = BASE + 0x93e6e8
CONSTRUCTOR_THUNK = BASE + 0x2d50e4
CONSTRUCTOR = BASE + 0x2d4680
VM_CHECK = BASE + 0x37792c
REGISTER = BASE + 0x1e1dc8
VENDOR_INIT = BASE + 0x1e1cdc
VENDOR_OPS = BASE + 0x931940
VENDOR_OPS_BYTES = 0x878
VENDOR_HOOKS = (BASE + 0x931188, BASE + 0x931180, BASE + 0x9311a8, BASE + 0x9321b8)
VENDOR_OPCODES = BASE + 0x931938
CALLBACKS = ((BASE + 0x1e1fbc, BASE + 0x1e2040), (BASE + 0x1e207c, BASE + 0x1e20dc))
CAVE = BASE + 0x2c00
CAVE_END = BASE + 0x3400
RET = 0xd65f03c0
NOP = 0xd503201f

def branch(pc, target, link=False):
    delta = target - pc
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('Unaligned or out-of-range branch')
    return (0x94000000 if link else 0x14000000) | ((delta // 4) & 0x3ffffff)

def cbnz(pc, reg, target):
    delta = target - pc
    if not 0 <= reg < 31 or delta % 4 or not -(1 << 20) <= delta < (1 << 20):
        raise ValueError('Invalid CBNZ')
    return 0x35000000 | (((delta // 4) & 0x7ffff) << 5) | reg

def mov(reg, value):
    if not 0 <= reg < 31 or not 0 <= value <= 0xffff:
        raise ValueError('Invalid MOVZ')
    return 0x52800000 | (value << 5) | reg

def address(pc, reg, target):
    pages = ((target & ~4095) - (pc & ~4095)) >> 12
    if not -(1 << 20) <= pages < (1 << 20):
        raise ValueError('Invalid ADRP')
    pages &= 0x1fffff
    return [0x90000000 | ((pages & 3) << 29) | ((pages >> 2) << 5) | reg,
            0x91000000 | ((target & 4095) << 10) | (reg << 5) | reg]

class Image:
    def __init__(self, path):
        self.original = Path(path).read_bytes()
        if hashlib.sha256(self.original).hexdigest() != ORIGINAL_SHA256:
            raise ValueError('Requires exact original 21H461 bluetoothd')
        self.data = bytearray(self.original)
        magic, cpu, subtype, filetype, count, commands_size, _, _ = struct.unpack_from('<8I', self.data)
        if (magic, cpu, subtype, filetype) != (0xfeedfacf, 0x100000c, 0, 2):
            raise ValueError('Not ordinary arm64 executable')
        self.segments = []; self.sections = []; self.uuid = None
        at = 32
        for _ in range(count):
            command, size = struct.unpack_from('<II', self.data, at)
            if size < 8 or at + size > 32 + commands_size:
                raise ValueError('Bad load command bounds')
            if command == 0x19:
                name, va, vs, off, fs, maximum, initial, n, flags = struct.unpack_from('<16sQQQQIIII', self.data, at + 8)
                self.segments.append((name.rstrip(b'\0').decode(), va, vs, off, fs, initial))
                if size != 72 + n * 80:
                    raise ValueError('Bad segment command size')
                for index in range(n):
                    sn, _, a, b, o = struct.unpack_from('<16s16sQQI', self.data, at + 72 + index * 80)
                    self.sections.append((sn.rstrip(b'\0').decode(), a, b, o))
            elif command == 0x1b:
                self.uuid = str(uuid.UUID(bytes=bytes(self.data[at + 8:at + 24])))
            at += size
        if at != 32 + commands_size or self.uuid != ORIGINAL_UUID:
            raise ValueError('Load commands or UUID differ')
        if not at <= CAVE - BASE < CAVE_END - BASE <= min(a for _, a, _, _ in self.sections) - BASE:
            raise ValueError('Cave overlaps load commands or section data')
        if any(self.data[CAVE - BASE:CAVE_END - BASE]):
            raise ValueError('Cave is not original zero padding')
        self.cursor = CAVE; self.edits = []; self.entries = {}; self.routed = False

    def offset(self, va):
        matches = [off + va - base for _, base, _, off, size, _ in self.segments if base <= va < base + size]
        if len(matches) != 1:
            raise ValueError('Unbacked or ambiguous virtual address')
        return matches[0]

    def put(self, va, words, expected):
        before = struct.pack('<' + 'I' * len(expected), *expected)
        after = struct.pack('<' + 'I' * len(words), *words)
        off = self.offset(va)
        if len(before) != len(after) or self.data[off:off + len(before)] != before:
            raise ValueError('Original instruction mismatch at ' + hex(va))
        if any(not (off + len(before) <= e['offset'] or e['offset'] + len(bytes.fromhex(e['after'])) <= off) for e in self.edits):
            raise ValueError('Overlapping edits')
        self.data[off:off + len(after)] = after
        self.edits.append({'address': hex(va), 'offset': off, 'before': before.hex(), 'after': after.hex()})

    def emit(self, name, words):
        start = self.cursor
        if start + len(words) * 4 > CAVE_END or name in self.entries:
            raise ValueError('Cave exhausted or duplicate entry')
        encoded = [word(start + index * 4) if callable(word) else word for index, word in enumerate(words)]
        self.put(start, encoded, [0] * len(encoded))
        self.cursor += len(encoded) * 4; self.entries[name] = start
        return start

    def routing(self):
        if self.routed:
            raise ValueError('Already routed')
        # The model constructor is real; type0 means native THIRD_PARTY_DONGLE.
        entry = self.emit('constructor', [mov(1, 0), lambda pc: branch(pc, CONSTRUCTOR)])
        self.put(CONSTRUCTOR_THUNK, [branch(CONSTRUCTOR_THUNK, entry)], [branch(CONSTRUCTOR_THUNK, CONSTRUCTOR)])
        self.put(BASE + 0x31cc90, [NOP], [0x34000400])
        # Only this daemon's cached VM getter changes; kernel identity is untouched.
        self.put(VM_CHECK, [mov(0, 0), RET], [0xa9bf7bfd, 0x910003fd])
        start = self.cursor
        entry = self.emit('registration', [lambda pc: cbnz(pc, 0, start + 8),
                            lambda pc: branch(pc, REGISTER + 12), 0x51000409,
                            lambda pc: branch(pc, REGISTER + 4)])
        self.put(REGISTER, [branch(REGISTER, entry)], [0x51000409])
        # Clear both previously selected vendor tables and hooks on type0 reopen.
        # The standard HCI initialization chain remains native and checks real replies.
        start = self.cursor
        words = [None] + address(start + 4, 0, VENDOR_OPS) + [mov(1, VENDOR_OPS_BYTES),
                                                       lambda pc: branch(pc, BASE + 0x708bcc, True)]
        for target in VENDOR_HOOKS:
            words += address(start + 4 * len(words), 8, target) + [0xf900011f]  # str xzr,[x8]
        words += address(start + 4 * len(words), 8, VENDOR_OPCODES) + [0xb900011f,
                                                     lambda pc: branch(pc, BASE + 0x1e1da0)]
        original = start + 4 * len(words)
        words[0] = lambda pc: cbnz(pc, 0, original)
        words += [0x51000408, lambda pc: branch(pc, BASE + 0x1e1cfc)]
        entry = self.emit('vendor_init', words)
        self.put(BASE + 0x1e1cf8, [branch(BASE + 0x1e1cf8, entry)], [0x51000408])
        for index, (site, epilogue) in enumerate(CALLBACKS):
            start = self.cursor
            original = 0x51000509 if index == 0 else 0x51000508
            entry = self.emit('callbacks_' + str(index), [lambda pc, t=start + 8: cbnz(pc, 8, t),
                                 lambda pc, t=epilogue: branch(pc, t), original,
                                 lambda pc, t=site + 4: branch(pc, t)])
            self.put(site, [branch(site, entry)], [original])
        self.routed = True
        return self

    def report(self):
        if not self.routed:
            raise ValueError('No routing performed')
        expected = bytearray(self.original)
        for edit in self.edits:
            data = bytes.fromhex(edit['after']); expected[edit['offset']:edit['offset'] + len(data)] = data
        if expected != self.data:
            raise ValueError('Unreported image modification')
        return {'original_sha256': ORIGINAL_SHA256, 'uuid': self.uuid,
                'output_sha256': hashlib.sha256(self.data).hexdigest(), 'edits': self.edits,
                'entries': {k: hex(v) for k, v in self.entries.items()}, 'stage': 'offline-model-type0-routing',
                'original_BlueTool_call_unchanged': True, 'signed': False, 'installed': False,
                'hardware_verified': False, 'capability_adapter': False}
