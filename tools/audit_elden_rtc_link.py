"""Audit the actual final RTC registration, callback and recovery call edges.

This is a component link audit, not a boot test or a global prelink ABI claim.
The initializer must only register the two actual native clock callbacks; the
recovery call must precede the existing storage shutdown in its real function.
"""
from pathlib import Path
import argparse, hashlib, json, re, struct, subprocess, sys


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def signed(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build', 'parent', 'rtc-build', 'prelink-tool', 'output'):
        p.add_argument('--' + key, type=Path, required=True)
    a = p.parse_args()
    sys.path.insert(0, str(a.prelink_tool))
    from native_prelink import macho, backed_offset, exports
    report = json.loads((a.kernel_build / 'result.json').read_text())
    native = a.kernel_build / 'native.macho'
    kernel = a.kernel_build / 'kernel.macho'
    if not report['passed'] or sha(native) != report['native_sha256'] or sha(kernel) != report['kernel_sha256']:
        raise ValueError('Final RTC image identity differs')
    raw = kernel.read_bytes(); image = macho(raw)
    original = (a.parent / 'kernel.macho').read_bytes(); old = macho(original)
    names = {n: v for v, group in exports(raw, image).items() for n in group}
    symbols = []
    for line in subprocess.check_output(['xcrun', 'nm', '-n', str(native)], text=True).splitlines():
        m = re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$', line)
        if m: symbols.append((int(m[1], 16), m[2]))

    def read(addr, count):
        off = backed_offset(image['segments'], addr, count, len(raw))
        return raw[off:off + count]

    def unique(name):
        hits = {addr for addr, n in symbols if n == name}
        if name in names: hits.add(names[name])
        if len(hits) != 1: raise ValueError('Unique actual symbol required: ' + name)
        return hits.pop()

    def initializers(blob, im):
        out = []
        for seg in im['segments']:
            for sec in seg['sections']:
                if sec['name'] == '__mod_init_func':
                    data = blob[sec['fileoff']:sec['fileoff'] + sec['size']]
                    if len(data) % 8: raise ValueError('Unaligned initializer table')
                    out.extend(struct.unpack('<%dQ' % (len(data) // 8), data))
        return out

    registration = unique('_armdt_platform_clock_register')
    clock_read = unique('__ZN12_GLOBAL__N_19clockReadEPy')
    clock_set = unique('__ZN12_GLOBAL__N_18clockSetEy')
    constructors = initializers(raw, image)
    if len(constructors) != len(initializers(original, old)) + 1:
        raise ValueError('RTC must add exactly one registration initializer')
    matched = []
    for addr in constructors:
        regs = {}; valid = True
        for off in range(0, 20, 4):
            insn = struct.unpack('<I', read(addr + off, 4))[0]; pc = addr + off
            if insn & 0x9f000000 == 0x90000000:  # ADRP
                imm = ((insn >> 5) & 0x7ffff) << 2 | ((insn >> 29) & 3)
                regs[insn & 31] = (pc & ~4095) + (signed(imm, 21) << 12)
            elif insn & 0xffc00000 == 0x91000000:  # ADD Xd,Xn,#imm12
                src = (insn >> 5) & 31
                if src not in regs: valid = False; break
                regs[insn & 31] = regs[src] + ((insn >> 10) & 4095)
            elif insn & 0xfc000000 == 0x14000000:  # exact tail B
                target = pc + (signed(insn & 0x3ffffff, 26) << 2)
                if target == registration:
                    if not valid or regs.get(0) != clock_read or regs.get(1) != clock_set or off != 16:
                        raise ValueError('RTC initializer calls incorrect callbacks')
                    matched.append(addr)
                break
            else: break
    if len(matched) != 1: raise ValueError('One register-only RTC initializer required')

    # Scan actual backed text sections; no disassembly/name inference for BL.
    calls = []
    rtc_stop = unique('_elden_rtc_stop_for_reboot')
    ufs_stop = unique('_elden_ufs_wait_shutdown')
    for seg in image['segments']:
        for sec in seg['sections']:
            if sec['name'] != '__text': continue
            base = sec.get('va')
            if base is None: raise ValueError('Text section address missing')
            data = raw[sec['fileoff']:sec['fileoff'] + sec['size']]
            for off in range(0, len(data) - 3, 4):
                insn = struct.unpack_from('<I', data, off)[0]
                if insn & 0xfc000000 == 0x94000000:
                    pc = base + off; target = pc + (signed(insn & 0x3ffffff, 26) << 2)
                    if target == rtc_stop: calls.append(pc)
    if len(calls) != 1: raise ValueError('One actual recovery RTC-stop call required')
    call = calls[0]
    starts = sorted({addr for addr, _ in symbols})
    end = next((addr for addr in starts if addr > call), call + 4096)
    following = []
    for pc in range(call + 4, end, 4):
        insn = struct.unpack('<I', read(pc, 4))[0]
        if insn & 0xfc000000 == 0x94000000:
            target = pc + (signed(insn & 0x3ffffff, 26) << 2)
            if target == ufs_stop: following.append(pc)
    if len(following) != 1: raise ValueError('RTC stop must precede UFS stop in same real function')
    # Direct read/learn and the current shared gate remain real linked exports.
    required = ['_elden_rtc_core_read', '_elden_rtc_core_learn', '_elden_rtc_spmi_read',
                '_elden_spmi_transaction_acquire', '_elden_spmi_transaction_release',
                '__ZN13ArmDTPlatform15getGMTTimeOfDayEv', '__ZN13ArmDTPlatform15setGMTTimeOfDayEl']
    edges = {n: hex(unique(n)) for n in required}
    metadata = json.loads((a.kernel_build / 'kernel.json').read_text())
    native_report = json.loads((a.rtc_build / 'result.json').read_text())
    from replay_xnu_objects import sections
    extent = {name: {k: {'bytes': v[0], 'flags': v[1], 'sha256': v[2]} for k, v in
                    sections(a.rtc_build / (Path(name).stem + '.o')).items()}
              for name in native_report['objects']}
    result = {'rtc_component_link_passed': True, 'kernel_sha256': sha(kernel), 'native_sha256': sha(native),
              'registration_only_initializer': hex(matched[0]), 'actual_clock_callbacks': [hex(clock_read), hex(clock_set)],
              'recovery_rtc_stop_call': hex(call), 'following_ufs_stop_call': hex(following[0]),
              'exports': edges, 'section_extents': extent, 'hardware': False,
              'legacy_flags_unchanged': {k: metadata.get(k) for k in
                ('abi_qualified', 'loadable', 'runtime_qualified', 'instruction_relocation_complete')}}
    a.output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS: one register-only RTC initializer, exact clock callbacks and RTC-before-UFS recovery call')


if __name__ == '__main__': main()
