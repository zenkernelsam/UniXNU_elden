"""Read-only MobileGestalt cache audit of the exact existing Y700 RAM Data seed."""
from pathlib import Path
import hashlib,json,os,plistlib,re,subprocess
root=Path('/Users/ciscohe/Desktop/UniXNU_elden')
source=root/'out/skip-setup-20261107/data.raw'
packed=root/'out/skip-setup-20261107/data.packed'
def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        while b:=f.read(8<<20):h.update(b)
    return h.hexdigest()
assert digest(source)=='5489da6ec3db4c96219b9cc3990798106b1246502bc934cadd5de83bdb76f044'
assert digest(packed)=='483125a27280f17d91de594f951cee4216f4888b764525b74db0f61ed62d34b3'
out=root/'out/gesture-cache-audit-20261109';mount=out/'mount';mount.mkdir(parents=True,exist_ok=True)
assert not any(mount.iterdir())
device=None
try:
    r=plistlib.loads(subprocess.check_output(['hdiutil','attach','-readonly','-nobrowse','-owners','off',
        '-imagekey','diskimage-class=CRawDiskImage','-mountpoint',str(mount),'-plist',str(source)]))
    entities=r['system-entities'];devices=[v['dev-entry'] for v in entities if 'dev-entry' in v]
    assert devices and re.fullmatch(r'/dev/disk\d+',devices[0])
    device=devices[0]
    assert len([v for v in entities if v.get('mount-point')==str(mount)])==1
    matches=[]
    for current,dirs,files in os.walk(mount,followlinks=False):
        dirs[:]=[d for d in dirs if not (Path(current)/d).is_symlink()]
        for name in files:
            if 'mobilegestalt' in name.lower():matches.append(str((Path(current)/name).relative_to(mount)))
    report=dict(source=str(source),raw_sha256=digest(source),packed_sha256=digest(packed),
        mobilegestalt_files=matches,read_only=True,seed_has_cached_identity=bool(matches))
finally:
    if device:subprocess.run(['hdiutil','detach',device],check=True)
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
