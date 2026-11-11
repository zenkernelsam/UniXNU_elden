"""Mac: export successful battery source, native archives and final kernel."""
import argparse,hashlib,json,shutil,zipfile
from pathlib import Path
ROOT=Path('/Users/ciscohe/Desktop/UniXNU_elden')
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--attempt',type=int,default=3)
a=p.parse_args();assert 2<=a.attempt<=9
version='v'+str(a.attempt)
OUT=ROOT/('out/startup-battery-telemetry-'+version+'-20261108')
COMPILED=ROOT/('out/battery-telemetry-'+version+'-20261108')
SOURCE=ROOT/('research/battery-telemetry-20261108/source-input-'+version)
TREE=ROOT/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
sha=lambda b:hashlib.sha256(b).hexdigest()
files={}
for p in SOURCE.iterdir():
    if p.is_file():files['drivers/EldenPmicGlink/'+p.name]=p.read_bytes()
for name in ['compile_battery_runtime-'+version+'_mac.py','build_startup_battery_telemetry-'+version+'.py',
             'prepare_battery_telemetry_derivative.py']:
    files['build-scripts/'+name]=(ROOT/'out'/name).read_bytes()
manifest=dict(files={n:sha(b) for n,b in files.items()},
    kernel_sha256=sha((OUT/'entry/probe/kernel.macho').read_bytes()),hardware_tested=False)
with zipfile.ZipFile(OUT/'compiled-source.zip','w',zipfile.ZIP_DEFLATED) as z:
    for n,b in files.items():z.writestr(n,b)
    z.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
shutil.copyfile(TREE/'build/sheng-hid-input/abi-audit.json',OUT/'hid-abi.json')
artifacts={'kernel.macho':OUT/'entry/probe/kernel.macho'}
for name in ['kernel.json','derivative.json','result.json','hid-abi.json','compiled-source.zip']:
    artifacts[name]=OUT/name
artifacts['compile-result.json']=COMPILED/'compile-result.json'
for group in ['EldenAdspRuntime','EldenPmicClient']:
    for p in (COMPILED/group).iterdir():
        if p.is_file() and p.suffix in ('.json','.a','.o'):artifacts['archives/'+group+'/'+p.name]=p
record=dict(files={n:sha(p.read_bytes()) for n,p in artifacts.items()},hardware_tested=False,
    source_files=len(files),defaults_off=True)
with zipfile.ZipFile(OUT/'artifacts.zip','w',zipfile.ZIP_DEFLATED) as z:
    for n,p in artifacts.items():z.write(p,n)
    z.writestr('artifact-hashes.json',json.dumps(record,indent=2)+'\n')
print(json.dumps(dict(archive_bytes=(OUT/'artifacts.zip').stat().st_size,source_files=len(files),
    kernel_sha256=record['files']['kernel.macho'],artifact_zip_sha256=sha((OUT/'artifacts.zip').read_bytes())),indent=2))
