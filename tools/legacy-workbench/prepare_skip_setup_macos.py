"""Prepare and audit a copy of the volatile Data seed with official skip-setup preferences."""
from pathlib import Path
import hashlib,json,os,plistlib,shutil,subprocess,sys
root=Path.cwd();out=root/'out/skip-setup-20261107';out.mkdir(exist_ok=True)
base=root/'out/ram-data-20261107/data.packed'
assert hashlib.sha256(base.read_bytes()).hexdigest()=='eb99e7370cfab4c3d58d97555632989b8f445a012a42f2f2acaab9189d5f8ba5'
tool=root/'third_party/UniXNU/scripts/seed-data-skip-setup.py'
review=root/'research/skip-setup-20261107';review.mkdir(exist_ok=True)
shutil.copyfile(tool,review/'seed-data-skip-setup.py')
sys.path.insert(0,str(root/'third_party/UniXNU/drivers/DarwinBoot/Test'));import packed_ramdisk
names=['com.apple.purplebuddy','com.apple.purplebuddy.notbackedup','com.apple.springboard','com.apple.migration','com.apple.managedconfiguration.notbackedup']
def inspect(raw,label):
    point=out/(label+'-mount');point.mkdir(exist_ok=True);assert not any(point.iterdir())
    attached=plistlib.loads(subprocess.check_output(['hdiutil','attach','-readonly','-nobrowse','-owners','on','-imagekey','diskimage-class=CRawDiskImage','-mountpoint',point,'-plist',raw]))
    dev=next(e['dev-entry'] for e in attached['system-entities'] if 'dev-entry' in e)
    try:
        info=plistlib.loads(subprocess.check_output(['diskutil','info','-plist',point]))
        record={'uuid':info['VolumeUUID'],'preferences':{}}
        for name in names:
            p=point/'mobile/Library/Preferences'/(name+'.plist')
            record['preferences'][name]=None if not p.exists() else dict(values=plistlib.loads(p.read_bytes()),uid=p.stat().st_uid,gid=p.stat().st_gid,mode=oct(p.stat().st_mode&0o777))
        return record
    finally:subprocess.run(['hdiutil','detach',dev],check=True)
oldraw=root/'out/ram-data-20261107/elden-data-512.cdr'
assert oldraw.stat().st_size==536870912 and hashlib.sha256(oldraw.read_bytes()).hexdigest()=='8104894952be1abadd7abaacbf2ca8d823c9789c525e2a3cbb2e57369745df33'
before=inspect(oldraw,'before');target=out/'data.packed'
(out/'before.json').write_text(json.dumps(before,indent=2)+'\n')
(root/'third_party/UniXNU/build').mkdir(exist_ok=True)
with (out/'prepare.log').open('w') as log:
    subprocess.run([sys.executable,tool,base,target,'--no-auto-dim'],stdout=log,stderr=subprocess.STDOUT,check=True)
logical,regions=packed_ramdisk.regions(target.read_bytes());assert logical==536870912
newraw=out/'data.raw'
with newraw.open('wb') as f:
    f.truncate(logical)
    for offset,data in regions:f.seek(offset);f.write(data)
after=inspect(newraw,'after');assert before['uuid']==after['uuid']=='E5D06647-CCB4-491D-A897-3C21B55703CF'
prefs=after['preferences'];assert prefs['com.apple.purplebuddy']['values']['SetupDone'] is True
assert prefs['com.apple.purplebuddy']['values']['SetupFinishedAllSteps'] is True
assert prefs['com.apple.migration']['values']['LastSystemVersion']=='21H461'
assert prefs['com.apple.springboard']['values']['SBDisableAutoDim'] is True
assert all(prefs[n]['uid']==501 and prefs[n]['mode']=='0o600' for n in names)
assert hashlib.sha256(base.read_bytes()).hexdigest()=='eb99e7370cfab4c3d58d97555632989b8f445a012a42f2f2acaab9189d5f8ba5'
record=dict(base_sha256=hashlib.sha256(base.read_bytes()).hexdigest(),packed_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),packed_bytes=target.stat().st_size,
    expanded_sha256=hashlib.sha256(newraw.read_bytes()).hexdigest(),expanded_bytes=logical,tool_sha256=hashlib.sha256(tool.read_bytes()).hexdigest(),
    before=before,after=after,original_seed_unchanged=True,device_tested=False)
(out/'audit.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
