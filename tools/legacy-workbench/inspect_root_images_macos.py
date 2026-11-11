"""Read-only APFS identity inspection of task-owned files inside this Mac repo."""
from pathlib import Path
import hashlib
import json
import plistlib
import subprocess

ROOT=Path.cwd().resolve()
WORK=ROOT/'out/prerequisites-20261106/root-images'
EXPECTED={'OS.raw':'d8808bea18b949142b3e47bbcb780c3f22faaacd9b40a88f77c23557181edc9c',
          'SystemOS.raw':'b4908cde41d23e6bc05e6af51a2c9eeb00cf0f863a6a18b1600c96a3fd70414f'}
report=[]
for name,wanted in EXPECTED.items():
    path=(WORK/name).resolve()
    if ROOT not in path.parents or not path.is_file():raise RuntimeError('image outside task repository')
    sha=hashlib.sha256()
    with path.open('rb') as stream:
        while chunk:=stream.read(8<<20):sha.update(chunk)
    if sha.hexdigest()!=wanted:raise RuntimeError('image SHA mismatch')
    result=subprocess.run(['hdiutil','attach','-readonly','-nomount','-nobrowse',
        '-imagekey','diskimage-class=CRawDiskImage','-plist',str(path)],capture_output=True,check=True)
    attach=plistlib.loads(result.stdout);entities=attach.get('system-entities',[])
    devices=[x['dev-entry'] for x in entities if 'dev-entry' in x]
    if not devices:raise RuntimeError('no image device returned')
    containers=[]
    try:
        for device in devices:
            data=subprocess.run(['diskutil','apfs','list','-plist',device],capture_output=True)
            if data.returncode:continue
            parsed=plistlib.loads(data.stdout)
            for container in parsed.get('Containers',[]):
                if container not in containers:containers.append(container)
        if not containers:raise RuntimeError('APFS identity unavailable')
        info={'name':name,'sha256':wanted,'read_only':True,'mounted':False,'containers':[]}
        for container in containers:
            info['containers'].append({key:container.get(key) for key in ('APFSContainerUUID','CapacityCeiling','ContainerReference')})
            info['containers'][-1]['volumes']=[{key:volume.get(key) for key in ('APFSVolumeUUID','Name','Roles','DeviceIdentifier','CapacityInUse')} for volume in container.get('Volumes',[])]
        report.append(info);(WORK/'identities.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(info,indent=2),flush=True)
    finally:
        # Only detach the virtual image device returned by this attach call.
        subprocess.run(['hdiutil','detach',devices[0]],check=True,capture_output=True)
print('PASS: both raw image hashes and native read-only APFS identities')
