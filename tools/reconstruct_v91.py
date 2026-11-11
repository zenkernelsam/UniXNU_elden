#!/usr/bin/env python3
"""Reconstruct recorded v91 sources from a pinned XNU Git object, without device I/O.

This verifies the source layer only. It does not claim a complete kernel build.
"""
from pathlib import Path
import argparse,hashlib,io,json,shutil,subprocess,tarfile

REPO=Path(__file__).resolve().parents[1]
PROFILE=REPO/'profiles/v91'
XNU='d8b80295118ef25ac3a784134bcf95cd8e88109f'

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def verify():
    manifest=json.loads((PROFILE/'manifest.json').read_text(encoding='utf-8'))
    for name,h in manifest['source_files'].items():
        p=PROFILE/name
        if not p.is_file() or digest(p)!=h:raise RuntimeError('Source mismatch: '+name)
    return manifest

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-only',action='store_true')
    parser.add_argument('--xnu-repository',type=Path)
    parser.add_argument('--output',type=Path)
    a=parser.parse_args();manifest=verify()
    if a.verify_only:
        print(json.dumps({'verified_source_files':len(manifest['source_files']),'full_build_verified':False}));return
    if not a.xnu_repository or not a.output:parser.error('--xnu-repository and --output are required')
    source=a.xnu_repository.resolve(strict=True);out=a.output.resolve()
    if out.exists():raise RuntimeError('Output must be new; existing trees are never reset')
    resolved=subprocess.check_output(['git','-C',str(source),'rev-parse',XNU+'^{commit}'],text=True).strip()
    if resolved!=XNU:raise RuntimeError('Pinned XNU object is missing')
    archive=subprocess.check_output(['git','-C',str(source),'archive','--format=tar',XNU])
    out.mkdir(parents=True);x=out/'xnu';x.mkdir()
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        for member in tar.getmembers():
            target=(x/member.name).resolve()
            if not target.is_relative_to(x):raise RuntimeError('Unsafe archive path')
            if member.isdev():raise RuntimeError('Unexpected archive device')
            if member.issym() or member.islnk():
                link=(target.parent/member.linkname if member.issym() else x/member.linkname).resolve()
                if not link.is_relative_to(x):raise RuntimeError('Unsafe archive link')
        tar.extractall(x,filter='data')
    subprocess.run(['git','init','--quiet',str(x)],check=True)
    patch=PROFILE/'reconstruction/xnu-working-tree.patch'
    for args in (['--check'],[]):
        subprocess.run(['git','-C',str(x),'apply',*args,str(patch)],check=True)
    shutil.copytree(PROFILE/'reconstruction/xnu-additions',x,dirs_exist_ok=True)
    # The compiler snapshot takes precedence over the captured host workspace.
    snapshot=PROFILE/'sources'
    mapped={}
    for p in (snapshot/'cpu/xnu').rglob('*'):
        if p.is_file():mapped[p]=x/p.relative_to(snapshot/'cpu/xnu')
    for p in (snapshot/'xnu').rglob('*'):
        if not p.is_file() or p.suffix=='.patch':continue
        rel=p.relative_to(snapshot/'xnu')
        mapped[p]=x/('bsd/miscfs' if rel.parts[0]=='bindfs' else 'bsd/kern')/rel
    for p,q in mapped.items():
        if not q.resolve().is_relative_to(x):raise RuntimeError('Overlay path escapes output')
        q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
        if digest(p)!=digest(q):raise RuntimeError('Overlay mismatch')
    shutil.copytree(snapshot,out/'compiled-inputs')
    result={'xnu_commit':XNU,'source_files_verified':len(manifest['source_files']),
            'snapshot_xnu_files_applied':len(mapped),'source_reconstruction_passed':True,
            'kernel_compiled':False,'boot_image_created':False,'device_operation':False,
            'remaining':['Regenerate external binary headers','Build all native driver archives',
                         'Build and prelink XNU without historical object dependencies',
                         'Package and qualify with owner-supplied external assets']}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
