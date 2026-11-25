"""Compile pinned production C++ GPU sources against a source-built XNU context.

This stage emits an archive and import/initializer inventory, not a linked kernel.
No historical object is linked; the provided capture only supplies compiler flags
and hash-checked source/header paths. Full goal acceptance additionally needs the
Elden adapter, final link and the userspace artifacts.
"""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, os, re, subprocess, sys
from replay_xnu_objects import global_symbols, sections
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('tree','base-native','xnu-source','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--submit',type=Path,help='Directory holding the derived NativeAdrenoSubmit.cpp/.h; it replaces the tree copy in the compile set (same stem, one object)')
    p.add_argument('--jobs',type=int,default=2,choices=range(1,5));a=p.parse_args()
    tree=a.tree.resolve();base=a.base_native.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    cap=json.loads((base/'capture.json').read_text());matches=[j for j in cap['jobs'] if j['file'].endswith('/NativeLegacyDisplay.cpp')]
    if len(matches)!=1:raise ValueError('Need the precise source-only C++ context')
    rec=matches[0];old=rec['file'].split('/research/',1)[0];project=base/'build/project'
    if old==rec['file']:raise ValueError('Unrecognized capture project root')
    def remap(v):return re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    # This derives the current already-staged source paths, never old checkout paths.
    for name,item in cap['files'].items():
        staged=Path(remap(name))
        if not staged.is_file() or sha(staged)!=item['sha256']:raise ValueError('XNU input mismatch: '+name)
    manifest=json.loads((tree/'elden-source-manifest.json').read_text())['files']
    for rel,item in manifest.items():
        if sha(tree/rel)!=item['sha256']:raise ValueError('GPU source mismatch: '+rel)
    gpu=tree/'drivers/Adreno740'
    board_owned={'NativeAdrenoGmuConfig.cpp','NativeAdrenoGmuPlatform.cpp','NativeAdrenoKernelTime.cpp','NativeAdrenoService.cpp'}
    # These four bind ShengGmu/SM8550 and must be replaced by the Elden
    # board adapter. Their absence remains an explicit full-link requirement.
    sources=sorted(x for x in gpu.glob('NativeAdreno*.cpp') if 'Test' not in x.name and x.name not in board_owned)
    if not sources:raise ValueError('No GPU production sources')
    submit_note=None
    vfs_overlay=None
    if a.submit:
        derived=a.submit.resolve()
        # Every derived unit that replaces a tree translation unit (same stem, one
        # object), and every derived header that shadows a tree header: the overlay
        # puts the derived layouts in front of every TU (including quoted includes
        # and downstream runtime units inheriting these flags).
        swapped={};overlaid={}
        contents=[]
        for stem in ('NativeAdrenoSubmit','NativeAdrenoVm','NativeAdrenoMemory','NativeAdrenoGpuContext','NativeAdrenoPublish'):
            dcpp=derived/(stem+'.cpp')
            tree_cpp=gpu/(stem+'.cpp')
            if dcpp.is_file():
                if tree_cpp not in sources:raise ValueError('Derived '+stem+' given but the tree has no '+stem+'.cpp to replace')
                sources=[dcpp if x==tree_cpp else x for x in sources]
                swapped[stem]={'derived_cpp_sha256':sha(dcpp),'tree_cpp_sha256':sha(tree_cpp)}
            dh=derived/(stem+'.h')
            if dh.is_file():
                if not (gpu/(stem+'.h')).is_file():raise ValueError('Derived '+stem+'.h given but the tree has no '+stem+'.h to shadow')
                contents.append({'type':'file','name':stem+'.h','external-contents':str(dh)})
                overlaid[stem+'_h_sha256']=sha(dh)
        if swapped:
            vfs_overlay=out/'derived-overlay.yaml'
            vfs_overlay.write_text(json.dumps({
                'version': 0,
                'case-sensitive': False,
                'fallthrough': True,
                'roots': [{'type':'directory','name':str(gpu),'contents':contents}],
            }, indent=2)+'\n')
            submit_note={'units':swapped,**overlaid,'vfs_overlay':str(vfs_overlay)}
    flags=[];i=0
    while i<len(rec['arguments']):
        v=rec['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==rec['file'] or v in ('-MD','-MP','-MMD'):i+=1;continue
        # This vendor-only warning group in the historical command is not
        # recognized by current Xcode. Remove the obsolete group, not -Werror.
        if v=='-Wno-unnecessary-virtual-specifier':i+=1;continue
        if v.startswith(('-DELDEN_','-DNATIVE_UFS_','-DUFS_ROOT_')):i+=1;continue
        flags.append(remap(v));i+=1
    xnu=a.xnu_source.resolve()
    if not (xnu/'bsd/sys/resource_private.h').is_file():raise ValueError('Full source XNU private headers required')
    includes=[gpu,tree/'kits/ArmSmmu4K',tree/'kits/AdrenoPM4',tree/'kits/AdrenoFirmware',tree/'kits/Sm8750Platform',xnu/'bsd']
    flags += ['-Werror',*[v for d in includes for v in ('-I',str(d))]]
    if vfs_overlay:flags += ['-ivfsoverlay',str(vfs_overlay)]
    compile_env=dict(os.environ,**(cap.get('environment') or {}))
    review_path=ROOT/'config/gpu-native-warnings.json'
    reviews=json.loads(review_path.read_text()) if review_path.is_file() else {}
    results={}
    def compile_one(src):
        obj=out/(src.stem+'.o');cmd=[*flags,str(src),'-o',str(obj)]
        proc=subprocess.run(cmd,cwd=remap(rec['directory']),env=compile_env,capture_output=True,timeout=180)
        log=proc.stdout+proc.stderr;(out/(src.stem+'.log')).write_bytes(log)
        item={'source_sha256':sha(src),'command':cmd,'returncode':proc.returncode,'warnings':log.count(b'warning:')}
        sites=[]
        for match in re.finditer(r'([^\n]+?):(\d+):\d+: warning:.*?\[(-W[^\]]+)\]',log.decode('utf-8','replace')):
            path=Path(match[1]);sites.append({'file':path.name,'line':int(match[2]),'group':match[3],'file_sha256':sha(path)})
        item['warning_sites']=sites
        reviewed=reviews.get(src.name,{})
        # A reviewed entry may list alternate forms under 'derived' (e.g. the
        # --submit derivation), each with its own source hash and sites.
        options=[reviewed]+list(reviewed.get('derived',[]))
        item['warnings_reviewed']=not item['warnings'] or any(
             o.get('source_sha256')==item['source_sha256'] and o.get('sites')==sites and
             len(sites)==item['warnings'] for o in options)
        if not proc.returncode:
            syms=global_symbols(obj);item.update(object_sha256=sha(obj),imports=[k for k,v in syms.items() if not v[0]&0xe],exports=[k for k,v in syms.items() if v[0]&0xe],sections=sections(obj))
        print(src.name,proc.returncode,flush=True);return src.name,item
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for name,item in pool.map(compile_one,sources):
            results[name]=item;(out/'progress.json').write_text(json.dumps(results,indent=2)+'\n')
    compiled=all(not x['returncode'] for x in results.values())
    ok=compiled and all(x['warnings_reviewed'] for x in results.values())
    report={'passed':ok,'sources':results,'capture_sha256':sha(base/'capture.json'),'source_manifest_sha256':sha(tree/'elden-source-manifest.json'),'board_adapters_pending':sorted(board_owned),'extra_xnu_resource_header_sha256':sha(xnu/'bsd/sys/resource_private.h'),'full_kernel_linked':False,'device_access':False}
    report['compiled']=compiled
    if submit_note:report['derived_submit']=submit_note
    report['warning_review_sha256']=sha(review_path) if review_path.is_file() else None
    if compiled:
        archive=out/'libEldenAdreno830.a';subprocess.run(['xcrun','ar','rcs',str(archive),*[str(out/(p.stem+'.o')) for p in sources]],check=True)
        report['archive_sha256']=sha(archive)
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    if not ok:raise SystemExit(1)
if __name__=='__main__':main()
