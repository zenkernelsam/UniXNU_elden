"""Freeze the exact successful Mac sources and this profile's output files."""
from pathlib import Path
import hashlib,json,shutil,zipfile
root=Path('/Users/ciscohe/Desktop/UniXNU_elden')
out=root/'out/wcn-native-20261108'
tree=root/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree'
names=json.loads((root/'out/bt-native-source-names.json').read_text())
sha=lambda b:hashlib.sha256(b).hexdigest()
sources={};data={}
for name in names:
    f=tree/name
    if not f.is_file():f=root/name
    assert f.is_file() and not f.is_symlink(),str(f)
    data[name]=f.read_bytes();sources[name]=sha(data[name])
for name in ('build_bt_native.py','prepare_bt_native_derivative.py'):
    data[name]=(root/'out'/name).read_bytes();sources[name]=sha(data[name])
kernel=out/'entry/probe/kernel.macho'
manifest={'files':sources,'kernel_sha256':sha(kernel.read_bytes()),
          'generated_firmware_header_excluded':True,'hardware_tested':False}
with zipfile.ZipFile(out/'compiled-source.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name,b in data.items():z.writestr(name,b)
    z.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
for src,dest in [('build/sheng-hid-input/abi-audit.json','hid-abi.json'),
                 ('build/native-legacy-display/elden-build.json','display-build-report.json'),
                 ('build/native-legacy-display/bluetooth-inputs.json','bluetooth-inputs.json')]:
    f=tree/src
    if dest=='display-build-report.json' and not f.exists():continue
    assert f.is_file(),str(f)
    shutil.copyfile(f,out/dest)
bundle={'kernel.macho':kernel}
for name in ['kernel.json','derivative.json','result.json','hid-abi.json','bluetooth-inputs.json','compiled-source.zip']:
    bundle[name]=out/name
record={'files':{name:sha(f.read_bytes()) for name,f in bundle.items()},
        'source_files':len(sources),'hardware_tested':False}
with zipfile.ZipFile(out/'artifacts.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name,f in bundle.items():z.write(f,name)
    z.writestr('artifact-hashes.json',json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2))
