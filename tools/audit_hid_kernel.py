"""Independently inspect linked v100 HID providers, calls and Apple text."""
import argparse,hashlib,json,struct
from pathlib import Path
from audit_entry_relocation import symbol,entry
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def at_va(data,va,size):
    offset=32
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        command,length=struct.unpack_from('<II',data,offset)
        if command==25:
            base,_,fileoff,extent=struct.unpack_from('<QQQQ',data,offset+24)
            if base<=va and va+size<=base+extent:return data[fileoff+va-base:fileoff+va-base+size]
        offset+=length
    raise ValueError('Address is not file backed')

def audit(build):
    result=json.loads((build/'result.json').read_text());plan=ROOT/'profiles/v100-hid-delivery/plan.json'
    kernel=build/'kernel.macho';native=build/'native.macho'
    if result['profile']!='v100-hid-delivery' or result['profile_sha256']!=sha(plan) or sha(kernel)!=result['kernel_sha256'] or sha(native)!=result['native_sha256']:
        raise ValueError('Unqualified HID build identity')
    expected={'NativeHidInput.late.o','EldenHidGlue.o'}
    changed={name for name,r in result['archive_members'].items() if r['before']!=r['archived_after']}
    if changed!=expected or set(result['changed_archive_members'])!=expected or not result['core_filelist_equal']:
        raise ValueError('Unexpected replacement scope')
    pre=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink']
    verify_original_executables(kernel,json.loads((build/'kernel.json').read_text()),pre['original_executable_sha256'])
    data=native.read_bytes();image=kernel.read_bytes();target=symbol(data,'_elden_touch_set_sink_v2');calls=[];offset=32
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        command,length=struct.unpack_from('<II',data,offset)
        if command==25:
            for i in range(struct.unpack_from('<I',data,offset+64)[0]):
                at=offset+72+i*80;va,size,fileoff=struct.unpack_from('<QQI',data,at+32);flags=struct.unpack_from('<I',data,at+64)[0]
                if not flags&0x80000400:continue
                for position in range(0,size-3,4):
                    word=struct.unpack_from('<I',data,fileoff+position)[0]
                    if word&0x7c000000!=0x14000000:continue
                    delta=word&0x3ffffff
                    if delta&(1<<25):delta-=1<<26
                    if va+position+delta*4==target:calls.append(va+position)
        offset+=length
    if len(calls)<2:raise ValueError('Registration/unregistration do not reach the v2 provider')
    for address in [target,*calls]:
        if at_va(data,address,4)!=at_va(image,address,4):raise ValueError('Prelink altered the new binding')
    start,return_target,word=entry(image)
    return {'accepted_static':True,'profile_sha256':sha(plan),'kernel_sha256':sha(kernel),'native_sha256':sha(native),
        'changed_archive_members':sorted(changed),'core_filelist_equal':True,'original_apple_executables_hashed':30,
        'callback_address':hex(target),'callback_branches':[hex(v) for v in calls],'prelinked_call_bytes_equal':True,
        'entry_address':hex(start),'entry_return_target':hex(return_target),'entry_word':word,
        'hardware_tested':False,'device_operations':False}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('New audit output required')
    report=audit(a.build);a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
