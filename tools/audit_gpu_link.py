"""Check GPU-specific native/original ABI edges without changing emitter claims."""
from pathlib import Path
import argparse,hashlib,json,struct,sys
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--linked',type=Path,required=True);p.add_argument('--prelink-tool',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
 raw=(a.linked/'kernel.macho').read_bytes();im=macho(raw);names={n:v for v,group in exports(raw,im).items() for n in group}
 def read(va,size):
  offset=backed_offset(im['segments'],va,size,len(raw));return raw[offset:offset+size]
 def word(va):return struct.unpack('<I',read(va,4))[0]
 abi=json.loads((ROOT/'profiles/v91/reconstruction/tree/scripts/data/ioaccelerator-base-abi.json').read_text())
 checks={}
 for address,digest in abi['original_entry_sha256'].items():
  if hashlib.sha256(read(int(address,16),32)).hexdigest()!=digest:raise ValueError('Original entry changed')
 checks['original_entries_sha256']=True
 size=names['_native_adreno_accelerator_instance_size'];op=word(size)
 if op&0xff80001f!=0x52800000 or ((op>>5)&0xffff)!=136 or word(size+4)!=0xd65f03c0:raise ValueError('Accelerator instance size is not exactly136')
 checks['accelerator_instance_bytes']=136
 parent=int(abi['metaclass'],16)
 if names['__ZN15IOAcceleratorES10gMetaClassE']!=parent:raise ValueError('Wrong original metaclass binding')
 if struct.unpack('<Q',read(names['__ZN14AGXAccelerator10superClassE'],8))[0]!=parent:raise ValueError('Wrong native superclass pointer')
 checks['original_superclass_pointer']=hex(parent)
 base=struct.unpack('<168Q',read(names['__ZTV9IOService']+16,168*8));child=struct.unpack('<168Q',read(names['__ZTV14AGXAccelerator']+16,168*8))
 diffs=[i for i in range(168) if base[i]!=child[i]]
 allowed={v for n,v in names.items() if 'AGXAccelerator' in n and any(part in n for part in ('copyProperty','newUserClient','getMetaClass','D0Ev','D1Ev','D2Ev'))}
 if len(diffs)!=7 or any(child[i] not in allowed for i in diffs):raise ValueError('Unexpected accelerator vtable override')
 checks['vtable_entries']=168;checks['owned_override_slots']=diffs
 def branches(symbol):
  start=names[symbol];following=sorted({v for v in names.values() if start<v<start+512});end=following[0] if following else start+128
  found=[]
  for pc in range(start,end,4):
   op=word(pc)
   if op&0x7c000000==0x14000000:
    imm=op&0x3ffffff;imm=imm-(1<<26) if imm&(1<<25) else imm;found.append(pc+4*imm)
  return found
 pairs={'__ZN14AGXAcceleratorC2EPK11OSMetaClass':int(abi['protected_compatible_constructor_aliases'][0],16),
        '__ZN14AGXAcceleratorD2Ev':int(abi['nondeleting_destructor'],16)}
 for symbol,target in pairs.items():
  if target not in branches(symbol):raise ValueError('Native base call does not reach exact original: '+symbol)
 checks['native_original_branches']={k:hex(v) for k,v in pairs.items()}
 init=[]
 for seg in im['segments']:
  for section in seg['sections']:
   if section['name']=='__mod_init_func':
    data=raw[section['fileoff']:section['fileoff']+section['size']];init+=list(struct.unpack('<%dQ'%(len(data)//8),data))
 for source in ('NativeAdrenoAccelerator.cpp','NativeGOPPresenter.cpp'):
  address=names['__GLOBAL__sub_I_'+source]
  if address in init:raise ValueError('Late initializer still runs automatically: '+source)
 checks['late_initializers_not_automatic']=True
 required=['_elden_gpu_start','_elden_gpu_quiesce_for_reboot','_elden_gpu_display_ready','_native_adreno_quiesce_for_reboot',
           '_native_gop_handoff_call','_native_gop_handoff_close','_native_gop_handoff_memory','_Sm8750RpmhWriteActive']
 if any(not names.get(k) for k in required):raise ValueError('Required GPU interface missing')
 checks['required_interfaces']={k:hex(names[k]) for k in required}
 if any(k in names for k in ('_boot_log_export_put','_native_pmap_user_mappings')):raise ValueError('Unprovided diagnostic unexpectedly bound')
 meta=json.loads((a.linked/'kernel.json').read_text())
 result={'gpu_specific_abi_checks_passed':True,'checks':checks,'kernel_sha256':hashlib.sha256(raw).hexdigest(),
         'legacy_emitter_flags':{k:meta.get(k) for k in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')},
         'scope':'GPU ABI edges only; does not relabel the experimental legacy emitter or assert hardware execution','hardware':False}
 a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'gpu_specific_abi_checks_passed':True,'override_slots':diffs,'global_emitter_flags_unchanged':True}))
if __name__=='__main__':main()
