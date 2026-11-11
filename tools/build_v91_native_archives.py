#!/usr/bin/env python3
"""Rebuild v91's 14 project archives from frozen source providers."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,io
ROOT=Path(__file__).resolve().parents[1];PROFILE=ROOT/'profiles/v91/native-build'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--xnu-source',type=Path,required=True);p.add_argument('--xnu-repository',type=Path,required=True)
    p.add_argument('--dependencies',type=Path,required=True,help='Git repositories named as in the profile dependency table')
    p.add_argument('--nvt-header',type=Path,required=True);p.add_argument('--payload-header',type=Path,required=True)
    p.add_argument('--llvm-objcopy',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--jobs',type=int,default=4,choices=range(1,9));a=p.parse_args()
    plan=json.loads((PROFILE/'plan.json').read_text());out=a.output.resolve()
    if out.exists():raise RuntimeError('Output must be a new directory')
    if plan['format']!=1 or plan['profile']!='v91-native':raise RuntimeError('Unexpected source profile')
    xnu=a.xnu_source.resolve(strict=True);clang=subprocess.check_output(['xcrun','--sdk','iphoneos','--find','clang'],text=True).strip()
    sdk=Path(subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip());resource=Path(subprocess.check_output([clang,'-print-resource-dir'],text=True).strip())
    out.mkdir(parents=True);dependency_files={}
    for name,dep in plan['dependencies'].items():
        requested=sorted({v['path'] for v in plan['providers'].values() if v['kind']=='dependency' and v['dependency']==name})
        if not requested:continue
        repository=(a.dependencies/name).resolve(strict=True)
        raw=subprocess.check_output(['git','-C',str(repository),'cat-file','--batch'],input=''.join(dep['commit']+':'+n+'\n' for n in requested).encode());stream=io.BytesIO(raw)
        for relative in requested:
            header=stream.readline().split()
            if len(header)!=3 or header[1]!=b'blob':raise RuntimeError('Pinned dependency blob unavailable: '+name+'/'+relative)
            data=stream.read(int(header[2]));assert stream.read(1)==b'\n';dest=(out/'dependency-sources'/name/relative).resolve()
            if not dest.is_relative_to(out/'dependency-sources'):raise RuntimeError('Dependency path escapes output')
            dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data);dependency_files[(name,relative)]=dest
    old=plan['virtual_root']
    def expand(s):return s.replace('@VROOT@',old).replace('@CLANG@',clang)
    files={}
    for virtual,item in plan['providers'].items():
        kind=item['kind'];base=None
        if kind=='repository':base=ROOT;source=base/item['path']
        elif kind=='reconstructed-xnu':base=xnu;source=base/item['path']
        elif kind=='profile-source':base=PROFILE;source=base/item['path']
        elif kind=='dependency':source=dependency_files[(item['dependency'],item['path'])]
        elif kind=='external-nvt':source=a.nvt_header.resolve(strict=True)
        else:raise RuntimeError('Unknown source provider')
        if base and not source.resolve().is_relative_to(base):raise RuntimeError('Provider escapes source root')
        if sha(source)!=item['sha256']:raise RuntimeError('Source hash mismatch: '+str(source))
        files[expand(virtual)]={'path':str(source),'sha256':item['sha256'],'bytes':source.stat().st_size}
    toolchain={}
    for name,item in plan['toolchain_files'].items():
        if '/usr/lib/clang/17/' in name:base=resource;actual=base/name.split('/usr/lib/clang/17/',1)[1]
        elif '.sdk/' in name:base=sdk;actual=base/name.split('.sdk/',1)[1]
        else:raise RuntimeError('Unknown toolchain header provider: '+name)
        if not actual.resolve().is_relative_to(base.resolve()) or sha(actual)!=item['sha256']:raise RuntimeError('Toolchain header mismatch: '+str(actual))
        toolchain[name]=dict(item,path=str(actual))
    jobs=[]
    for job in plan['jobs']:
        source=expand(job['file']);obj=expand(job['object']);args=[source if v=='@SOURCE@' else obj if v=='@OUTPUT@' else expand(v) for v in plan['templates'][job['template']]]
        jobs.append({'original_object':obj,'directory':expand(job['directory']),'file':source,'arguments':args,'objcopy':job.get('objcopy',[]),'expected_sections':job['expected_sections'],'expected_symbols':job['expected_symbols']})
    capture={'jobs':jobs,'files':files,'toolchain_files':toolchain,'environment':{'SDKROOT':str(sdk)},'missing':[],'no_dependency_records':[],'profile_sha256':sha(PROFILE/'plan.json'),'historical_objects_required':False}
    cap=out/'capture.json';cap.write_text(json.dumps(capture,indent=2)+'\n')
    subprocess.run([sys.executable,str(ROOT/'tools/replay_xnu_objects.py'),'--capture',str(cap),'--historical-root',old,'--output',str(out/'build'),'--payload-header',str(a.payload_header.resolve()),'--nvt-header',str(a.nvt_header.resolve()),'--xnu-repository',str(a.xnu_repository.resolve()),'--llvm-objcopy',str(a.llvm_objcopy.resolve()),'--jobs',str(a.jobs)],check=True)
    layouts=out/'layouts';layouts.mkdir()
    for name,layout in plan['archives'].items():
        file=layouts/(name+'.json');file.write_text(json.dumps(layout,indent=2)+'\n')
        subprocess.run([sys.executable,str(ROOT/'tools/archive_replayed_objects.py'),'--capture',str(cap),'--results',str(out/'build'),'--layout',str(file),'--output',str(out/'archives'/name)],check=True)
if __name__=='__main__':main()
