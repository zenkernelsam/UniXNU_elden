"""Describe the real CPU8 archive objects without modifying frozen old reports."""
from pathlib import Path
import hashlib,json,shutil
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/cpu-guard-20261111';old=r/'research/cpu-full-20261111'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
e=json.loads((old/'external-build.json').read_text());records={}
for group,row in e['groups'].items():
    original=Path(row['original_build_record']);meta=json.loads(original.read_text())
    dest=p/'archives'/group;archive=dest/Path(row['archive']).name
    assert sha(archive)==row['archive_sha256']
    objects={}
    for obj in row['objects']:
        f=dest/obj['name'];assert sha(f)==obj['object_sha256']
        assert sha(Path(obj['source']))==obj['source_sha256']
        objects[obj['name']]=obj['object_sha256']
    meta.update(archive=str(archive),archive_sha256=sha(archive),archive_bytes=archive.stat().st_size,
                objects=objects,commands=[o['command'] for o in row['objects']],
                reused_from_cpu8=True,compiler_config={'MAX_CPUS':8,'MAX_CPU_CLUSTERS':2},
                cpu8_compilation_record=str(old/'external-build.json'),cpu8_compilation_record_sha256=sha(old/'external-build.json'))
    if 'reused' in meta:meta['reused']={'from':'consistent CPU8 build','objects':objects}
    (dest/'build.json').write_text(json.dumps(meta,indent=2)+'\n');records[group]=meta['archive_sha256']
    if group=='EldenBacklight':
        for name in ('OriginalBacklightABI.h','original-bindings.S','abi-audit.json'):
            shutil.copyfile(original.parent/name,dest/name)
(p/'archives/compile-result.json').write_text(json.dumps({'arm64_ios17':True,'cpu8_archives':records,'original_compilation_record':str(old/'external-build.json'),'metadata_only':True,'hardware_tested':False},indent=2)+'\n')
source=r/'out/startup-cpu-guard-v81-20261111';out=r/'out/startup-basic-bundle-v81-20261108';out.mkdir(exist_ok=False)
for name in ('kernel.macho','kernel.json','result.json'):shutil.copyfile(source/name,out/name)
print(json.dumps({'archives':records,'prelink_sha256':sha(out/'kernel.macho'),'signed':False},indent=2))
