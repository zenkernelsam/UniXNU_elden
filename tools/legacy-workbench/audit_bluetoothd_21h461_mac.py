"""Read-only original OS mount and native Bluetooth inputs audit in the Y700 repo."""
from pathlib import Path
import hashlib,json,plistlib,shutil,subprocess
root=Path('/Users/ciscohe/Desktop/UniXNU_elden')
out=root/'research/wcn-native-20261108/original-21H461';out.mkdir(parents=True,exist_ok=True)
image=root/'out/prerequisites-20261106/root-images/OS.raw'
digest=hashlib.sha256()
with image.open('rb') as f:
    while chunk:=f.read(8*1024*1024):digest.update(chunk)
assert digest.hexdigest()=='d8808bea18b949142b3e47bbcb780c3f22faaacd9b40a88f77c23557181edc9c'
mount=root/'research/wcn-native-20261108/mount/OS';mount.mkdir(parents=True,exist_ok=True)
result=subprocess.run(['hdiutil','attach','-readonly','-nobrowse','-imagekey',
    'diskimage-class=CRawDiskImage','-mountpoint',str(mount),str(image)],capture_output=True,text=True)
(out/'mount.log').write_text(result.stdout+result.stderr)
result.check_returncode()
try:
    version=plistlib.loads((mount/'System/Library/CoreServices/SystemVersion.plist').read_bytes())
    assert version['ProductBuildVersion']=='21H461' and version['ProductVersion']=='17.7.11',version
    binary=mount/'usr/sbin/bluetoothd';data=binary.read_bytes()
    points=[(0x2ef860,bytes.fromhex('ff8304d1e9230c6d'),bytes.fromhex('00008052c0035fd6')),
            (0xddd20,bytes.fromhex('a0050034'),bytes.fromhex('1f2003d5'))]
    checks=[]
    for offset,old,new in points:
        observed=data[offset:offset+len(old)]
        checks.append({'offset':offset,'expected':old.hex(),'observed':observed.hex(),'matches':observed==old,'replacement':new.hex()})
    launch=mount/'System/Library/LaunchDaemons/com.apple.bluetoothd.plist'
    job=plistlib.loads(launch.read_bytes())
    shutil.copyfile(binary,out/'bluetoothd')
    shutil.copyfile(launch,out/'com.apple.bluetoothd.plist')
    report={'version':version,'os_image_sha256':digest.hexdigest(),'bluetoothd_bytes':len(data),
            'bluetoothd_sha256':hashlib.sha256(data).hexdigest(),'fixups':checks,
            'launchd':job,'read_only':True,'modified_original':False}
    (out/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['bluetoothd_bytes','bluetoothd_sha256','fixups','read_only']},indent=2))
finally:
    subprocess.run(['hdiutil','detach',str(mount)],check=True,capture_output=True)
