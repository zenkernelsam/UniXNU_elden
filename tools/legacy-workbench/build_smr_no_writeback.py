"""Local Mac image: preserve SMR scan addresses while avoiding LDR writeback."""
from pathlib import Path
import os,sys,shutil,subprocess,hashlib,struct,json
ROOT=Path('/Users/ciscohe/Desktop/UniXNU_elden');os.chdir(ROOT)
sys.path.insert(0,str(ROOT/'scripts'));import make_entry_probe_kernel as maker
out=ROOT/'out/fix-smmu-load-form-20261106';out.mkdir(exist_ok=False)
env=os.environ.copy();env['PATH']=str(ROOT/'.cache/mtools/bin')+os.pathsep+env['PATH']
payload=out/'payload.img';shutil.copyfile(ROOT/'out/fix-cb0-read-form-20261106/payload.img',payload)
subprocess.run(['mcopy','-o','-i',str(payload),'::EFI/UniXNU/Payloads/kernel.macho',str(out/'kernel-original.macho')],check=True,env=env)
old=(out/'kernel-original.macho').read_bytes();assert hashlib.sha256(old).hexdigest()=='884269a05fd272f6b7c2c417a885b9c05b71fb5bfea8ae705aa3e38cd1eea7a5'
va=0xfffffff007967fe0;_,off=maker.locate(old,va,4,executable=True)
before=maker.assemble(Path('/usr/bin/clang'),maker.STUB_HEADER+'  ldr w15, [x14], #4\n')
after=maker.assemble(Path('/usr/bin/clang'),maker.STUB_HEADER+'  ldr w15, [x14, x12, lsl #2]\n')
assert old[off:off+4]==before and len(after)==4
new=old[:off]+after+old[off+4:]
(out/'kernel.macho').write_bytes(new)
report={'kernel_sha256':hashlib.sha256(new).hexdigest(),'patch_va':hex(va),'patch_file_offset':off,
 'before':before.hex(),'after':after.hex(),'address_equivalence':'iteration i: old advances cursor by 4; new retains base and uses x12=i with a scaled offset x12*4; x14 is redefined before subsequent use',
 'slot_count_unchanged':True,'no_mmio_writes_added':True}
(out/'patch.json').write_text(json.dumps(report,indent=2)+'\n')
subprocess.run(['mcopy','-o','-i',str(payload),str(out/'kernel.macho'),'::EFI/UniXNU/Payloads/kernel.macho'],check=True,env=env)
subprocess.run(['python3','scripts/build_phase2_image.py','--kind','boot','--fd','out/phase3/old-fd-verified.bin',
 '--payload',str(payload),'--output',str(out/'boot.img'),'--embed-payload','--work',str(out),'--clang','/usr/bin/clang'],check=True,env=env)
print(json.dumps(report,indent=2))
