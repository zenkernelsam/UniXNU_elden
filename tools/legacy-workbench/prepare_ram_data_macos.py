"""Create a fresh 512MiB test APFS Data seed inside this Mac Y700 repository.

Only the verified original OS image supplies initial /private/var contents.
No host data, keys, account records or other-device seed is used.
"""
from pathlib import Path
import hashlib,json,os,plistlib,shutil,stat,subprocess
root=Path.cwd().resolve();out=root/'out/ram-data-20261107';out.mkdir(exist_ok=True)
source=root/'out/prerequisites-20261106/root-images/OS.raw';image=out/'elden-data-512.cdr'
def digest(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  while b:=f.read(8<<20):h.update(b)
 return h.hexdigest()
assert digest(source)=='d8808bea18b949142b3e47bbcb780c3f22faaacd9b40a88f77c23557181edc9c'
if image.exists():raise RuntimeError('refuse to replace existing seed')
sm=out/'os-mount';dm=out/'data-mount'
for p in (sm,dm):p.mkdir(exist_ok=True);assert not any(p.iterdir())
def attach(p,m,ro=False):
 args=['hdiutil','attach','-nobrowse','-owners','off','-mountpoint',str(m),'-plist']
 if ro:args+=['-readonly','-imagekey','diskimage-class=CRawDiskImage']
 r=plistlib.loads(subprocess.check_output(args+[str(p)]))
 nodes=r['system-entities'];devices=[x['dev-entry'] for x in nodes if 'dev-entry' in x]
 volumes=[x for x in nodes if x.get('mount-point')==str(m)]
 if not devices or len(volumes)!=1:raise RuntimeError('unexpected owned image attachment')
 return devices[0],volumes[0]['dev-entry']
sdev=ddev=None;files=links=0;created=[]
try:
 sdev,_=attach(source,sm,True)
 assert digest(sm/'sbin/launchd')=='8cec9a3c44a443b08e977ccc5d577850316ad428c575dc01f09b018dbd5b6a25'
 src=sm/'private/var';assert src.is_dir() and not src.is_symlink() and sm in src.resolve().parents
 subprocess.run(['hdiutil','create','-size','512m','-layout','NONE','-fs','APFS','-volname','EldenData','-type','UDTO',str(image)],check=True)
 ddev,volume=attach(image,dm)
 info=plistlib.loads(subprocess.check_output(['diskutil','info','-plist',volume]))
 for current,dirs,names in os.walk(src,followlinks=False):
  dirs[:]=[n for n in dirs if n!='staged_system_apps']
  dest=dm/Path(current).relative_to(src);assert dest==dm or dm in dest.resolve().parents
  dest.mkdir(exist_ok=True);os.chmod(dest,stat.S_IMODE(Path(current).stat().st_mode))
  for name in list(dirs)+names:
   a,b=Path(current)/name,dest/name
   if a.is_symlink():
    os.symlink(os.readlink(a),b);links+=1
    if name in dirs:dirs.remove(name)
   elif a.is_file():shutil.copy2(a,b);files+=1
 extra=('db/dslocal/nodes/Default/users','db/dslocal/nodes/Default/groups','db/dslocal/nodes/Default/config',
        'containers/Data/System','containers/Data/Application','containers/Shared/SystemGroup','PersonaVolumes',
        'preboot','root','tmp','run','Library','keybags/backup','mobile/Library/Preferences','mobile/Library/Caches',
        'mobile/Library/Logs','mobile/Containers','mobile/Documents','mobile/Media','mobile/tmp')
 for name in extra:
  p=dm/name
  if p.is_symlink() or (p.resolve()!=dm and dm not in p.resolve().parents):raise RuntimeError('seed directory escapes image: '+name)
  if not p.exists():p.mkdir(parents=True);created.append(name)
 os.chmod(dm/'tmp',0o1777)
finally:
 for dev in (ddev,sdev):
  if dev:subprocess.run(['hdiutil','detach',dev],check=True)
assert image.stat().st_size==512*(1<<20)
with image.open('rb') as f:assert f.read(36)[32:36]==b'NXSB'
report=dict(path=str(image),bytes=image.stat().st_size,sha256=digest(image),volume_uuid=info['VolumeUUID'],
            initial_source='verified original OS private/var excluding staged_system_apps',files=files,symlinks=links,
            added_directories=created,added_personal_keys_or_accounts=False,persistent=False,
            permissions='source modes preserved; created via owners-off test image; device ownership/provisioning unqualified')
(out/'seed.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
