"""Mac: preserve the accepted entry/AMFI transformations on the display build."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys
root=Path.cwd();out=root/'out/wcn-power-20261108'
base=out/'kernel.macho';data=base.read_bytes();digest=hashlib.sha256(data).hexdigest()
assert digest==json.loads((out/'result.json').read_text())['kernel_sha256']
plan=json.loads((out/'kernel.json').read_text());assert plan['candidate_sha256']==digest
assert plan['original_executable_sha256']['com.apple.iokit.IOMobileGraphicsFamily']=='2e044ee68c1650397e1435234ca6459206262e4c7ed709cb7131c6f664f9bd9b'
sys.path.insert(0,str(root/'scripts'));import make_entry_probe_kernel as maker
assert struct.unpack_from('<I',data,maker.locate(data,0xfffffff004aaecfc)[1])[0]==0xaa1503e0
adt=root/'out/arm-io-phandle-fix-20261106/kernel.macho.adt'
assert hashlib.sha256(adt.read_bytes()).hexdigest()=='464f1053f4f2c1afef03da4bc638a06c1401c5be799eb156d8b5078eb1fa0a42'
subprocess.run([sys.executable,root/'scripts/make_entry_probe_kernel.py','--kernel',base,'--adt',adt,
    '--expect-kernel-bytes',str(len(data)),'--expect-kernel-sha256',digest,'--output-dir',out/'entry',
    '--clang','/usr/bin/clang','--poke','0xfffffff004aaecfc=0x17ffffd4'],check=True)
new=(out/'entry/probe/kernel.macho').read_bytes()
ncmds,cmdbytes=struct.unpack_from('<II',data,16)
segments=maker.segments(data);newsegments=maker.segments(new);assert newsegments[:-1]==segments
from darwinboot_preflight import parse_kernel
old=parse_kernel(data);stub=newsegments[-1]
assert stub['name']=='__ELDEN_PROBE' and stub['file_size']==104 and stub['protection']==5
entry=old['entry_file_offset'];amfi=maker.locate(data,0xfffffff004aaecfc)[1]
allowed=[(16,24),(32+cmdbytes,32+cmdbytes+72),(entry,entry+4),(amfi,amfi+4)]
changed=[i for i,(a,b) in enumerate(zip(data,new)) if a!=b]
assert all(any(a<=i<b for a,b in allowed) for i in changed)
report=dict(source_sha256=digest,derivative_sha256=hashlib.sha256(new).hexdigest(),allowed_ranges=allowed,
    changed_existing_bytes=len(changed),display_driver_bytes_preserved=True,device_tested=False)
(out/'derivative.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
