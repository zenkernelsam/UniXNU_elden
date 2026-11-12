"""Read the linked WCN kernel: verify call targets, shared owners and firmware."""
import argparse,hashlib,json,re,struct,subprocess
from pathlib import Path
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('build','firmware','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();profile=ROOT/'profiles/v97-wcn/plan.json';plan=json.loads(profile.read_text())
    result=json.loads((a.build/'result.json').read_text());kernel=a.build/'kernel.macho';native=a.build/'native.macho'
    if result['profile_sha256']!=sha(profile) or sha(kernel)!=result['kernel_sha256'] or sha(native)!=result['native_sha256']:
        raise ValueError('Build identity differs')
    pre=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink']
    verify_original_executables(kernel,json.loads(kernel.with_suffix('.json').read_text()),pre['original_executable_sha256'])
    text=subprocess.check_output([str(a.llvm/'llvm-nm'),str(native)],text=True)
    symbols={}
    for match in re.finditer(r'^([0-9a-f]+) (\S) (.+)$',text,re.M):
        symbols.setdefault(match[3],[]).append((int(match[1],16),match[2]))
    required=['_elden_wcn_request_stop','_elden_wcn_stop_requested','_elden_wcn_power_start',
              '_elden_bt_host_publish','_elden_bt_host_run','_sm8750_aoss_qmp_owner','_sm8750_gcc_vote_owner']
    for name in required:
        if len(symbols.get(name,[]))!=1:raise ValueError('Missing or duplicated provider: '+name)
    data=native.read_bytes();at=32;code=[]
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        command,length=struct.unpack_from('<II',data,at)
        if command==0x19:
            for i in range(struct.unpack_from('<I',data,at+64)[0]):
                off=at+72+i*80;va,size,fileoff=struct.unpack_from('<QQI',data,off+32)
                flags=struct.unpack_from('<I',data,off+64)[0]
                if flags&0x80000400:code.append((va,size,fileoff))
        at+=length
    targets={symbols[name][0][0]:name for name in required[:3]}
    branches={name:[] for name in required[:3]}
    for va,size,fileoff in code:
        for offset in range(0,size-3,4):
            word=struct.unpack_from('<I',data,fileoff+offset)[0]
            if word&0x7c000000!=0x14000000:continue
            displacement=word&0x3ffffff
            if displacement&(1<<25):displacement-=1<<26
            target=va+offset+(displacement<<2)
            if target in targets:branches[targets[target]].append(hex(va+offset))
    if any(not calls for calls in branches.values()):raise ValueError('Linked startup/stop branch target missing')
    image=kernel.read_bytes()
    def at_va(blob,va,size):
        at=32
        for _ in range(struct.unpack_from('<I',blob,16)[0]):
            cmd,length=struct.unpack_from('<II',blob,at)
            if cmd==0x19:
                base,extent,fileoff,filesize=struct.unpack_from('<QQQQ',blob,at+24)
                if base<=va and va+size<=base+filesize:return blob[fileoff+va-base:fileoff+va-base+size]
            at+=length
        raise ValueError('Call/provider is outside file-backed segments')
    for name,calls in branches.items():
        for address in [symbols[name][0][0],*(int(v,16) for v in calls)]:
            if at_va(image,address,4)!=at_va(data,address,4):raise ValueError('Prelink changed an internal WCN call/provider')
    # Object relocations identify the intended callers; final native branches
    # above prove these providers are present at real linked code addresses.
    callers={}
    for obj,name in [('ShengUfs','_elden_wcn_request_stop'),('EldenDisplayGlue','_elden_wcn_power_start'),('NativeHost','_elden_wcn_stop_requested')]:
        relocs=subprocess.check_output([str(a.llvm/'llvm-objdump'),'--reloc',str(a.build/'objects'/(obj+'.o'))],text=True)
        if not re.search(r'ARM64_RELOC_BRANCH26\s+'+re.escape(name)+r'\b',relocs):raise ValueError('Caller relocation missing: '+obj)
        callers[obj]=name
    firmware={}
    for item in plan['firmware']:
        f=a.firmware/item['file'];payload=f.read_bytes()
        if len(payload)!=item['bytes'] or sha(f)!=item['sha256']:raise ValueError('Firmware source differs')
        count=image.count(payload)
        if count!=1:raise ValueError('Firmware is absent or duplicated: '+item['file'])
        firmware[item['file']]={'sha256':item['sha256'],'complete_copies':count}
    report={'accepted_static':True,'kernel_sha256':sha(kernel),'native_sha256':sha(native),
            'profile_sha256':sha(profile),'callers':callers,'linked_branches':branches,'prelinked_call_bytes_equal':True,
            'single_shared_owner_symbols':{n:hex(symbols[n][0][0]) for n in required[-2:]},
            'firmware':firmware,'original_apple_executables_hashed':30,
            'generated_source_hashes':{n:sha(a.build/'source'/n) for n in ('BtBridge.c','BtBridge.h','Sm8750Rpmh.c','EldenWcnFirmware.c','EldenWcnFirmware.h')},
            'boot_options_audited':False,'hardware_tested':False,'boot_packaged':False,'device_operations':False}
    if a.output.exists():raise ValueError('Audit output must be new')
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
