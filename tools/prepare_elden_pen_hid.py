"""Qualify the original OS17 HID tables and prepare pen/native touch sources.

The input must be a source-built, prelinked Elden kernel with its full metadata.
This does not link or install a new kernel. Original HID code must remain exact.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, struct, sys
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build','prelink-tool','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,backed_offset,exports
    path=a.kernel_build/'kernel.macho';data=path.read_bytes();meta=json.loads((a.kernel_build/'kernel.json').read_text())
    if sha(path)!=meta['candidate_sha256']:raise ValueError('Candidate metadata hash differs')
    image=macho(data);names={n:address for address,group in exports(data,image).items() for n in group}
    def read(address,n):
        at=backed_offset(image['segments'],address,n,len(data));return data[at:at+n]
    extent=next(e for e in meta['extents'] if e['module']=='com.apple.iokit.IOHIDFamily' and e['segment']=='__TEXT_EXEC')
    expected='7e4c5104814f2e773d5be13d092dec53ee44ba5a61a81264b229780406e86b72'
    if hashlib.sha256(read(extent['va'],extent['size'])).hexdigest()!=expected:raise ValueError('Original IOHIDFamily code differs')
    device,driver,event=0xfffffff005d045c0,0xfffffff005d06a68,0xfffffff005d07318
    native_service=struct.unpack('<168Q',read(names['__ZTV9IOService']+16,168*8))
    native_object=struct.unpack('<14Q',read(names['__ZTV8OSObject']+16,14*8))
    tables={};overrides={}
    for label,address,count,parent,expected_owned in [('device',device,207,native_service,57),('driver',driver,220,native_service,69),('event',event,37,native_object,26)]:
        table=struct.unpack('<%dQ'%count,read(address,count*8));tables[label]=table
        if read(address+count*8,8)!=bytes(8):raise ValueError('Wrong table boundary: '+label)
        owned={i:v for i,v in enumerate(table) if i>=len(parent) or v!=parent[i]}
        if len(owned)!=expected_owned:raise ValueError('Unexpected original override count: '+label)
        for slot,target in owned.items():
            if label=='device' and slot==180 and target==names['___cxa_pure_virtual']:continue
            if not extent['va']<=target<extent['va']+extent['size']:raise ValueError('Private target outside original HID code')
        overrides[label]={str(k):hex(v) for k,v in owned.items()}
    known={16:0xfffffff004afb494,18:0xfffffff004afcf90,23:0xfffffff004afcfb8,24:0xfffffff004afefdc,25:0xfffffff004afe4c4,27:0xfffffff004afef64}
    if any(tables['event'][i]!=address for i,address in known.items()) or tables['driver'][198]!=0xfffffff004aface8:
        raise ValueError('Qualified event methods differ')
    if tables['device'][181]!=0xfffffff004add0c4:raise ValueError('Original handleReport differs')
    fields=json.loads((ROOT/'drivers/EldenPen/EventFields.json').read_text())
    if not fields['passed'] or struct.unpack('<II',read(0xfffffff004afad08,8))!=(0xf9408008,0xf940b900):
        raise ValueError('Qualified original pending-message layout differs')
    # Slot26's typed setFixedValue contract comes from the same2031 event ABI
    # used by the OS17 donor. Its target/32-byte body are bound in this record.
    header='// Generated after original OS17 HID code/table qualification.\n#pragma once\n'
    for name,value in [('DRIVER_VPTR',driver),('EVENT_VPTR',event),('ORIGINAL_DISPATCH',tables['driver'][198]),('SET_FIXED',tables['event'][26])]:
        header+='#define ELDEN_PEN_'+name+' '+hex(value)+'ULL\n'
    for name,value in [('EVENT_DATA_OFFSET',16),('DRIVER_EXPANSION_OFFSET',256),('PENDING_OFFSET',368),
                       ('ORIENTATION_OFFSET',64),('ALTITUDE_OFFSET',68),('POLAR_MIN_BYTES',76)]:
        header+='#define ELDEN_PEN_'+name+' '+str(value)+'\n'
    (out/'EldenPenOriginalAbi.h').write_text(header)
    original_header=ROOT/'profiles/v91/native-build/extra/47901aa7a085/OriginalHidDeviceABI.h'
    shutil.copyfile(original_header,out/'OriginalHidDeviceABI.h')
    sys.path.insert(0,str(ROOT/'drivers/EldenPen'));from TouchSource import derive
    (out/'EldenTouchRuntime.cpp').write_bytes(derive())
    for name in ('EldenPenHid.cpp','EldenPenNative.h','HidReport.c','HidReport.h','PenReport.c','PenReport.h','Delivery.c','Delivery.h','PressureHook.inc','PoseHook.inc','Tilt.c','Tilt.h','TiltConstants.h'):
        shutil.copyfile(ROOT/'drivers/EldenPen'/name,out/name)
    report={'qualified':True,'reference_kernel_sha256':sha(path),'original_hid_text_sha256':expected,
        'private_overrides':overrides,'event_set_fixed_target':hex(tables['event'][26]),
        'event_set_fixed_first32_sha256':hashlib.sha256(read(tables['event'][26],32)).hexdigest(),
        'field_audit_sha256':sha(ROOT/'drivers/EldenPen/EventFields.json'),
        'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'hardware':False,'full_link':False}
    (out/'source.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: exact original HID code/private tables and derived pen/touch source')
if __name__=='__main__':main()
