"""Check the linked pen's original/native HID edges without changing global
experimental prelink qualification flags or claiming hardware acceptance.
"""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build','prelink-tool','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,backed_offset,exports
    raw=(a.kernel_build/'kernel.macho').read_bytes();im=macho(raw);names={n:v for v,group in exports(raw,im).items() for n in group}
    def read(address,size):
        offset=backed_offset(im['segments'],address,size,len(raw));return raw[offset:offset+size]
    # Inline virtuals are local symbols in the linked native image; the
    # prelink export dictionary intentionally omits them. Read the real native
    # symbol table and bind their bytes to the final image, not a name guess.
    native_path=a.kernel_build/'native.macho';native=native_path.read_bytes();native_image=macho(native)
    report=json.loads((a.kernel_build/'result.json').read_text())
    if hashlib.sha256(native).hexdigest()!=report['native_sha256']:raise ValueError('Native image identity differs')
    for line in subprocess.check_output(['xcrun','nm','-n',str(native_path)],text=True).splitlines():
        match=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if match and '11EldenPenHid' in match[2]:
            address=int(match[1],16)
            if match[2] in names and names[match[2]]!=address:raise ValueError('Native/prelink symbol address differs')
            names[match[2]]=address
    parent=struct.unpack('<207Q',read(0xfffffff005d045c0,207*8))
    child=struct.unpack('<207Q',read(names['__ZTV11EldenPenHid']+16,207*8))
    differences=[i for i in range(207) if parent[i]!=child[i]]
    expected={0:'__ZN11EldenPenHidD1Ev',1:'__ZN11EldenPenHidD0Ev',7:'__ZNK11EldenPenHid12getMetaClassEv',
              14:'__ZN11EldenPenHid4freeEv',86:'__ZN11EldenPenHid5startEP9IOService',87:'__ZN11EldenPenHid4stopEP9IOService',
              111:'__ZNK11EldenPenHid11getWorkLoopEv',171:'__ZNK11EldenPenHid18newTransportStringEv',
              173:'__ZNK11EldenPenHid16newProductStringEv',180:'__ZNK11EldenPenHid19newReportDescriptorEPP18IOMemoryDescriptor'}
    if differences!=sorted(expected):raise ValueError('Unexpected pen vtable override slots')
    matched={}
    for slot,name in expected.items():
        if name not in names or child[slot]!=names[name]:raise ValueError('Wrong method at slot '+str(slot)+': '+name)
        address=names[name];at=backed_offset(native_image['segments'],address,32,len(native))
        if native[at:at+32]!=read(address,32):raise ValueError('Native method code changed in prelink')
        matched[str(slot)]=name
    base_meta=names['__ZN11IOHIDDevice10gMetaClassE']
    if struct.unpack('<Q',read(names['__ZN11EldenPenHid10superClassE'],8))[0]!=base_meta:raise ValueError('Wrong HID superclass')
    initializers=[]
    for segment in im['segments']:
        for section in segment['sections']:
            if section['name']=='__mod_init_func':
                data=raw[section['fileoff']:section['fileoff']+section['size']]
                initializers+=list(struct.unpack('<%dQ'%(len(data)//8),data))
    late=names['__GLOBAL__sub_I_EldenPenHid.cpp']
    if late in initializers:raise ValueError('Pen metaclass still initializes automatically')
    required=['_elden_pen_native_start','_elden_pen_native_packet','_elden_pen_native_fault','_elden_pen_native_stop',
              '_elden_pen_hid_report','_elden_pen_pressure_fixed','_elden_touch_runtime_start','_elden_touch_request_stop']
    if any(n not in names for n in required):raise ValueError('Missing pen/touch interface')
    meta=json.loads((a.kernel_build/'kernel.json').read_text())
    result={'pen_specific_abi_checks_passed':True,'kernel_sha256':hashlib.sha256(raw).hexdigest(),
            'device_vtable_slots':207,'override_slots':differences,'override_methods':matched,'original_superclass':hex(base_meta),
            'late_initializer_not_automatic':True,'interfaces':{n:hex(names[n]) for n in required},
            'legacy_flags_unchanged':{k:meta.get(k) for k in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')},
            'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: linked pen HID inheritance,207slots/10overrides,late initialization and interfaces')
if __name__=='__main__':main()
