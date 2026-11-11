#!/usr/bin/env python3
"""Compile catalog-pinned RamBlock with Elden's qualified original storage ABI.

No mount/authentication stubs. The UFS archive owns the shared original imports.
The selected RAM seed and actual volume UUID remain explicit build inputs.
"""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tomllib
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/ram-block';OUT.mkdir(parents=True,exist_ok=True)
entry=next(e for e in tomllib.loads((ROOT/'components.toml').read_text())['component'] if e['id']=='RamBlock')
revision=entry['revision']
if revision!='25c57e0e9b68bf452dd1986914525edc9d2320bc':raise SystemExit('Re-audit changed RamBlock source')
repository=Path(os.environ['ELDEN_RAM_BLOCK_REPOSITORY']).resolve()
source=OUT/'src';source.mkdir(exist_ok=True);hashes={}
for name in ('NativeRamBlock.cpp','SparseDisk.h','SeedDisk.h','PersistOverlay.h','DirectDisk.h','LICENSE','AGENTS.md','README.md'):
    data=subprocess.check_output(['git','-C',repository,'show',revision+':'+name])
    (source/name).write_bytes(data);hashes[name]=hashlib.sha256(data).hexdigest()
data_store=os.environ.get('XNU_ELDEN_DATA_STORE')=='1'
if data_store:
    import runpy
    edits=runpy.run_path(str(ROOT/'scripts/patch-elden-data-store.py'))['patch_overlay'](source)
    (OUT/'data-overlay-patch.json').write_text(json.dumps(edits,indent=2)+'\n')
record=json.loads((ROOT/'build/sheng-ufs/compile-command.json').read_text())
old=next(x for x in record if x.endswith('/src/NativeUfs.cpp'))
command=[str(source/'NativeRamBlock.cpp') if x==old else x for x in record]
command[command.index('-o')+1]=str(OUT/'NativeRamBlock.o')
command+=['-I',str(source)]
if data_store:command+=['-DELDEN_DATA_STORE=1']
base=json.loads((ROOT/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
(OUT/'compile-command.json').write_text(json.dumps(command,indent=2)+'\n')
r=subprocess.run(command,cwd=base['directory'],capture_output=True,text=True)
(OUT/'compile.log').write_text(r.stdout+r.stderr)
if r.returncode:raise SystemExit(r.stderr[-5000:])
subprocess.run([sys.executable,ROOT/'scripts/build-elden-ufs-bindings.py','--component','NativeRamBlock',
    '--audit',os.environ['ELDEN_STORAGE_ABI_AUDIT'],'--candidate',os.environ['ELDEN_STORAGE_ABI_CANDIDATE'],
    '--plan',os.environ['ELDEN_STORAGE_ABI_PLAN'],'--directory',OUT,
    '--reuse-bindings',ROOT/'build/sheng-ufs/bindings.json'],check=True)
(OUT/'source-record.json').write_text(json.dumps({'revision':revision,'hashes':hashes,'ram_only_requested':not data_store,
    'auth_stubs':False,'data_overlay_patched':data_store},indent=2)+'\n')
