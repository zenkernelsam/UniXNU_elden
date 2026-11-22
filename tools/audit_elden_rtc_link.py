"""Audit the actual final RTC registration, callback and recovery call edges.

This is a component link audit, not a boot test or a global prelink ABI claim.
The initializer must only register the two actual native clock callbacks; the
recovery call must precede the existing storage shutdown in its real function.
"""
from pathlib import Path
import argparse, bisect, hashlib, json, re, struct, subprocess, sys


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def signed(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build', 'parent', 'rtc-build', 'prelink-tool', 'output'):
        p.add_argument('--' + key, type=Path, required=True)
    p.add_argument('--retained-rtc', action='store_true', help='Require the existing RTC initializer/provider to be retained from parent')
    a = p.parse_args()
    for key in ('kernel_build','parent','rtc_build','prelink_tool'):
        setattr(a,key,getattr(a,key).resolve(strict=True))
    sys.path.insert(0, str(a.prelink_tool))
    from native_prelink import macho, backed_offset, exports
    report = json.loads((a.kernel_build / 'result.json').read_text())
    native = a.kernel_build / 'native.macho'
    kernel = a.kernel_build / 'kernel.macho'
    if not report['passed'] or sha(native) != report['native_sha256'] or sha(kernel) != report['kernel_sha256']:
        raise ValueError('Final RTC image identity differs')
    raw = kernel.read_bytes(); image = macho(raw)
    original = (a.parent / 'kernel.macho').read_bytes(); old = macho(original)
    parent_report=json.loads((a.parent/'result.json').read_text())
    if hashlib.sha256(original).hexdigest()!=parent_report['kernel_sha256'] or sha(a.parent/'native.macho')!=parent_report['native_sha256']:
        raise ValueError('Actual RTC parent image identity differs')
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
    if len(constructors) != len(initializers(original, old)) + (0 if a.retained_rtc else 1):
        raise ValueError('RTC initializer count differs from explicit new/retained mode')
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
    if a.retained_rtc:
        # Verify the actual parent's register-only constructor and callback
        # pair as well. Count equality alone does not prove RTC was preserved.
        oldnames={n:v for v,group in exports(original,old).items() for n in group}
        oldsymbols={}
        for line in subprocess.check_output(['xcrun','nm','-n',str(a.parent/'native.macho')],text=True).splitlines():
            m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
            if m:oldsymbols.setdefault(m[2],set()).add(int(m[1],16))
        def oldunique(name):
            values=oldsymbols.get(name,set()).copy()
            if name in oldnames:values.add(oldnames[name])
            if len(values)!=1:raise ValueError('Actual parent RTC symbol absent: '+name)
            return values.pop()
        def oldread(addr,count):
            at=backed_offset(old['segments'],addr,count,len(original));return original[at:at+count]
        oldregistration=oldunique('_armdt_platform_clock_register')
        oldreadfn=oldunique('__ZN12_GLOBAL__N_19clockReadEPy');oldsetfn=oldunique('__ZN12_GLOBAL__N_18clockSetEy')
        oldmatched=[]
        for addr in initializers(original,old):
            regs={}
            for off in range(0,20,4):
                insn=struct.unpack('<I',oldread(addr+off,4))[0];pc=addr+off
                if insn&0x9f000000==0x90000000:
                    imm=((insn>>5)&0x7ffff)<<2|((insn>>29)&3);regs[insn&31]=(pc&~4095)+(signed(imm,21)<<12)
                elif insn&0xffc00000==0x91000000:
                    src=(insn>>5)&31
                    if src not in regs:break
                    regs[insn&31]=regs[src]+((insn>>10)&4095)
                elif insn&0xfc000000==0x14000000:
                    target=pc+(signed(insn&0x3ffffff,26)<<2)
                    if target==oldregistration:
                        if off!=16 or regs.get(0)!=oldreadfn or regs.get(1)!=oldsetfn:raise ValueError('Parent RTC constructor is not register-only')
                        oldmatched.append(addr)
                    break
                else:break
        if len(oldmatched)!=1:raise ValueError('Parent must contain exactly one actual RTC registration constructor')
        # The exact qualified RTC objects remain linked once with unchanged
        # bytes. Existing archives are checked by their actual inventory/SHA.
        oldcommand=json.loads((a.parent/'link-command.json').read_text());command=json.loads((a.kernel_build/'link-command.json').read_text())
        actual=json.loads((a.rtc_build/'result.json').read_text())
        for name,row in actual['objects'].items():
            obj=a.rtc_build/(Path(name).stem+'.o')
            if sha(obj)!=row['object_sha256']:raise ValueError('Qualified RTC source object changed: '+name)
            def linked(command):
                # ArmDTPlatform lives in its inherited force-loaded archive;
                # the four RTC-only objects are direct command arguments.
                if command.count(str(obj)):
                    if command.count(str(obj))!=1:raise ValueError('Duplicate direct RTC object')
                    return 1
                count=0
                for argument in command:
                    if not argument.startswith('-Wl,-force_load,'):continue
                    archive=Path(argument.split(',',2)[2]);members=subprocess.check_output(['ar','t',archive],text=True).splitlines()
                    if obj.name not in members:continue
                    body=subprocess.check_output(['ar','p',archive,obj.name])
                    if hashlib.sha256(body).hexdigest()!=row['object_sha256']:
                        # Same filename is also used by unrelated PMIC objects.
                        # Their complete archives are already retained; only
                        # libEldenRtc is this constructor's own RTC provider.
                        if archive.name=='libEldenRtc.a':raise ValueError('Actual inherited RTC archive member changed: '+name)
                        continue
                    count+=1
                return count
            if linked(oldcommand)!=1 or linked(command)!=1:raise ValueError('Original qualified RTC object not retained exactly once: '+name)
        spmi_before=[];spmi_after=[]
        for command,target in ((oldcommand,spmi_before),(command,spmi_after)):
            for argument in command:
                if not argument.startswith('-Wl,-force_load,'):continue
                archive=Path(argument.split(',',2)[2]);members=subprocess.check_output(['ar','t',archive],text=True).splitlines()
                if 'Spmi.o' in members:target.append((archive.name,hashlib.sha256(subprocess.check_output(['ar','p',archive,'Spmi.o'])).hexdigest()))
        if not spmi_before or sorted(spmi_before)!=sorted(spmi_after):raise ValueError('Actual RTC/PMIC SPMI members not retained')

    # Scan actual backed text sections; no disassembly/name inference for BL.
    calls = []
    all_calls = []
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
                if insn & 0x7c000000 == 0x14000000:
                    pc = base + off; target = pc + (signed(insn & 0x3ffffff, 26) << 2)
                    all_calls.append((pc, target, insn & 0xfc000000 == 0x94000000))
                    if target == rtc_stop and insn & 0xfc000000 == 0x94000000: calls.append(pc)
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
    # Match the receipt against the linked image before applying the historical
    # or packaged audit. Packaged owner code must have every
    # public entry in the export table, none in __mod_init_func, no direct
    # B/BL caller outside the owner's own function spans, and no 8-byte data
    # reference in any non-__LINKEDIT section. This static scan covers direct
    # calls and aligned pointers only; indirect BLR/BR or computed targets are
    # outside its coverage and are not claimed either way.
    native_report = json.loads((a.rtc_build / 'result.json').read_text())
    owner_names = ['_elden_rtc_owner_init', '_elden_rtc_owner_bind_nvmem', '_elden_rtc_owner_bind_alarm',
                   '_elden_rtc_owner_restore', '_elden_rtc_owner_learn', '_elden_rtc_owner_alarm_set',
                   '_elden_rtc_owner_alarm_read', '_elden_rtc_owner_alarm_ack', '_elden_rtc_owner_stop',
                   '_elden_rtc_owner_retire', '_elden_rtc_owner_status']
    owner_row = native_report['objects'].get('Owner.c')
    exported_owner = {n for n in names if n.startswith('_elden_rtc_owner_')}
    native_owner = {n for _, n in symbols if n.startswith('_elden_rtc_owner_')}
    expected_owner = set(owner_names) if owner_row is not None else set()
    if exported_owner != expected_owner or native_owner != expected_owner:
        raise ValueError('Linked RTC owner symbols disagree with build receipt')
    owner = {'owner_packaged': False, 'owner_absent': True}
    if owner_row is not None:
        if sha(a.rtc_build / 'Owner.o') != owner_row['object_sha256']:
            raise ValueError('Qualified RTC owner object changed')
        entries = sorted(unique(n) for n in owner_names)
        if any(v in constructors for v in entries): raise ValueError('Owner entry initializes automatically')
        starts_sorted = sorted({addr for addr, _ in symbols})
        def next_symbol(v):
            at = bisect.bisect_right(starts_sorted, v)
            return starts_sorted[at] if at < len(starts_sorted) else 1 << 64
        spans = [(v, next_symbol(v)) for v in entries]
        entry_set = set(entries)
        foreign = [(pc, t) for pc, t, link in all_calls
                   if t in entry_set and not any(lo <= pc < hi for lo, hi in spans)]
        if foreign: raise ValueError('Unrelated caller reaches owner entry: ' + hex(foreign[0][0]))
        for seg in image['segments']:
            if seg['name'] == '__LINKEDIT': continue
            for sec in seg['sections']:
                body = raw[sec['fileoff']:sec['fileoff'] + sec['size']]
                for v in entries:
                    if body.find(struct.pack('<Q', v)) != -1:
                        raise ValueError('Owner entry referenced by ' + seg['name'] + '/' + sec['name'])
        owner = {'owner_packaged': True, 'owner_absent': False,
                 'owner_exports': {n: hex(unique(n)) for n in owner_names},
                 'owner_not_automatically_initialized': True,
                 'owner_not_called_or_referenced': True,
                 'owner_inertness_scope': 'direct B/BL call sites in __text plus 8-byte data references in non-__LINKEDIT sections; indirect BLR/BR or computed calls are outside this static scan'}
    # Direct read/learn and the current shared gate remain real linked exports.
    required = ['_elden_rtc_core_read', '_elden_rtc_core_learn', '_elden_rtc_spmi_read',
                '_elden_spmi_transaction_acquire', '_elden_spmi_transaction_release',
                '__ZN13ArmDTPlatform15getGMTTimeOfDayEv', '__ZN13ArmDTPlatform15setGMTTimeOfDayEl']
    edges = {n: hex(unique(n)) for n in required}
    metadata = json.loads((a.kernel_build / 'kernel.json').read_text())
    from replay_xnu_objects import sections
    extent = {name: {k: {'bytes': v[0], 'flags': v[1], 'sha256': v[2]} for k, v in
                    sections(a.rtc_build / (Path(name).stem + '.o')).items()}
              for name in native_report['objects']}
    result = {'rtc_component_link_passed': True, 'kernel_sha256': sha(kernel), 'native_sha256': sha(native),
              'registration_only_initializer': hex(matched[0]), 'actual_clock_callbacks': [hex(clock_read), hex(clock_set)],
              'recovery_rtc_stop_call': hex(call), 'following_ufs_stop_call': hex(following[0]),
              'exports': edges, 'section_extents': extent, 'owner': owner,
              'provider_bound': False, 'nvmem_provider_installed': False,
              'alarm_provider_installed': False, 'boot_packaged': False, 'hardware': False,
              'legacy_flags_unchanged': {k: metadata.get(k) for k in
                ('abi_qualified', 'loadable', 'runtime_qualified', 'instruction_relocation_complete')}}
    result['retained_existing_RTC_mode']=a.retained_rtc
    a.output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS: one register-only RTC initializer, exact clock callbacks and RTC-before-UFS recovery call')


if __name__ == '__main__': main()
