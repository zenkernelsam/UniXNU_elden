"""Check the linked pen's original/native HID edges without changing global
experimental prelink qualification flags or claiming hardware acceptance.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build','prelink-tool','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,backed_offset,exports
    raw=(a.kernel_build/'kernel.macho').read_bytes();im=macho(raw);names={n:v for v,group in exports(raw,im).items() for n in group}
    def read(address,size):
        offset=backed_offset(im['segments'],address,size,len(raw));return raw[offset:offset+size]
    parent=struct.unpack('<207Q',read(0xfffffff005d045c0,207*8))
    child=struct.unpack('<207Q',read(names['__ZTV11EldenPenHid']+16,207*8))
    differences=[i for i in range(207) if parent[i]!=child[i]]
    allowed={v for n,v in names.items() if '11EldenPenHid' in n and any(s in n for s in ('D0Ev','D1Ev','D2Ev','getMetaClass','4free','5start','4stop','getWorkLoop','newTransportString','newProductString','newReportDescriptor'))}
    if len(differences)!=10 or any(child[i] not in allowed for i in differences):raise ValueError('Unexpected pen vtable override')
    if child[180] not in {v for n,v in names.items() if '11EldenPenHid' in n and 'newReportDescriptor' in n}:raise ValueError('Wrong descriptor slot')
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
            'device_vtable_slots':207,'override_slots':differences,'original_superclass':hex(base_meta),
            'late_initializer_not_automatic':True,'interfaces':{n:hex(names[n]) for n in required},
            'legacy_flags_unchanged':{k:meta.get(k) for k in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')},
            'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: linked pen HID inheritance,207slots/10overrides,late initialization and interfaces')
if __name__=='__main__':main()
