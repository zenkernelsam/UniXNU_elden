"""Verify profile userland fixup inputs in readonly, task-owned APFS images.

Never writes the images or applies patches. All mount points and outputs stay
inside the calling Y700 repository. This is input preparation, not boot proof.
"""
from pathlib import Path
import hashlib
import json
import plistlib
import subprocess
import tomllib

ROOT=Path.cwd().resolve();WORK=ROOT/'out/prerequisites-20261106'
profile=tomllib.loads((ROOT/'third_party/UniXNU/profiles/os/ipados-17.7.11-21H461/profile.toml').read_text())
fixups=[]
for value in profile['payload']['text_fixups']:
    name,offset,old,new=value.split(',');fixups.append(dict(name=name,offset=int(offset,16),old=old,new=new,matches=[]))
for name in ('OS.raw','SystemOS.raw'):
    image=(WORK/'root-images'/name).resolve();mount=(WORK/('mount-'+name.replace('.raw',''))).resolve()
    if ROOT not in image.parents or ROOT not in mount.parents:raise ValueError('outside task repository')
    mount.mkdir(exist_ok=True)
    if any(mount.iterdir()):raise ValueError('mountpoint is not empty')
    result=subprocess.run(['hdiutil','attach','-readonly','-nobrowse','-imagekey','diskimage-class=CRawDiskImage',
        '-mountpoint',str(mount),'-plist',str(image)],capture_output=True,check=True)
    info=plistlib.loads(result.stdout);devices=[x['dev-entry'] for x in info['system-entities'] if 'dev-entry' in x]
    try:
        for item in fixups:
            for candidate in mount.rglob(item['name']):
                actual=candidate.resolve()
                if mount not in actual.parents or not actual.is_file():continue
                with actual.open('rb') as stream:stream.seek(item['offset']);observed=stream.read(len(bytes.fromhex(item['old'])))
                item['matches'].append(dict(image=name,path=str(candidate.relative_to(mount)),
                    observed=observed.hex(),original_bytes_match=observed.hex()==item['old']))
        version=mount/'System/Library/CoreServices/SystemVersion.plist'
        if version.is_file():
            values=plistlib.loads(version.read_bytes())
            print(name,{k:values.get(k) for k in ('ProductVersion','ProductBuildVersion')},flush=True)
    finally:subprocess.run(['hdiutil','detach',devices[0]],capture_output=True,check=True)
report=dict(profile='ipados-17.7.11-21H461',patches_applied=False,read_only=True,fixups=fixups,
            all_fixups_uniquely_verified=all(len(x['matches'])==1 and x['matches'][0]['original_bytes_match'] for x in fixups))
(WORK/'root-fixup-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
if not report['all_fixups_uniquely_verified']:raise SystemExit(1)
