#!/usr/bin/env python3
"""Compile pinned, build-only peripheral cores for the Elden iOS17 kernel ABI.

This creates archives, not a boot image. No hardware startup or device command
is executed. Board resource qualification remains separate from compilation.
"""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,struct,subprocess,time

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def run(args):
    r=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=120)
    if r.returncode:raise RuntimeError(' '.join(map(str,args))+'\n'+r.stdout+r.stderr)
    return r.stdout
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sources',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--jobs',type=int,default=4);p.add_argument('--kernel-tree',type=Path)
    p.add_argument('--archiver',default='llvm-ar',help='LLVM archiver with deterministic D support')
    a=p.parse_args()
    if not 1<=a.jobs<=8:p.error('jobs must be1..8')
    source=a.sources.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((source/'manifest.json').read_text())
    sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path']).strip()
    clang=run(['xcrun','--find','clang']).strip();clangxx=run(['xcrun','--find','clang++']).strip()
    began=time.monotonic()
    def build(group):
        folder=source/group['name'];dest=out/group['name'];dest.mkdir(exist_ok=True);objects=[];commands=[]
        for f in group['files']:
            path=folder/f['file']
            if sha(path)!=f['sha256']:raise RuntimeError('source hash differs '+str(path))
            if path.suffix not in ('.c','.cpp'):continue
            cpp=path.suffix=='.cpp';obj=dest/(path.stem+'.o')
            cmd=[clangxx if cpp else clang,'-target','arm64-apple-ios17.0','-isysroot',sdk,
                 '-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-common','-mgeneral-regs-only',
                 '-DKERNEL','-O2','-Wall','-Wextra','-Werror','-I',folder,'-c',path,'-o',obj]
            cmd+=['-std=c++17','-fno-exceptions','-fno-rtti'] if cpp else ['-std=c11']
            cwd=None
            context=f.get('compile_context',group.get('compile_context'))
            if context is None and group['name']=='EldenBluetoothFirmware':context='bsd'
            if context is not None:
                if context not in ('bsd','iokit'):raise RuntimeError('unknown kernel compile context '+str(context))
                if not a.kernel_tree:raise RuntimeError(group['name']+' requires the actual XNU header/compile context')
                tree=a.kernel_tree.resolve()
                relative={'bsd':'bsd/DEVELOPMENT/vfs_subr.o.json',
                          'iokit':'iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'}[context]
                record=json.loads((tree/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837'/relative).read_text().strip().rstrip(','))
                args=record['arguments'];cmd=[];i=0
                while i<len(args):
                    value=args[i]
                    if value==record['file']:cmd.append(str(path))
                    elif value in ('-o','-MF','-MT','-MQ','-MJ'):
                        if value=='-o':cmd+=['-o',str(obj)]
                        i+=1
                    elif value=='-Xclang' and i+1<len(args) and args[i+1].startswith('-W'):i+=1
                    elif not value.startswith('-W') and value not in ('-MD','-MP','-MMD'):cmd.append(value)
                    i+=1
                cmd+=['-Wall','-Wextra','-Werror','-mgeneral-regs-only','-I',str(folder)]
                if cpp:cmd+=['-std=c++17','-fno-exceptions','-fno-rtti']
                cwd=record['directory']
            r=subprocess.run(list(map(str,cmd)),cwd=cwd,capture_output=True,text=True,timeout=120)
            if r.returncode:
                (dest/'compile-error.log').write_text(' '.join(map(str,cmd))+'\n'+r.stdout+r.stderr)
                raise RuntimeError('compile failed '+group['name']+'; see '+str(dest/'compile-error.log')+'\n'+r.stderr[:1200])
            raw=obj.read_bytes()
            if len(raw)<32 or struct.unpack_from('<II',raw) != (0xfeedfacf,0x100000c):raise RuntimeError('not an arm64 Mach-O object '+str(obj))
            # Check actual section descriptors, not a raw string search.
            ncmds=struct.unpack_from('<I',raw,16)[0];offset=32
            for _ in range(ncmds):
                command,size=struct.unpack_from('<II',raw,offset)
                if command==0x19:
                    nsec=struct.unpack_from('<I',raw,offset+64)[0]
                    for j in range(nsec):
                        name=raw[offset+72+j*80:offset+88+j*80].split(b'\0')[0]
                        if name==b'__mod_init_func':raise RuntimeError('automatic constructor in build-only core '+str(obj))
                offset+=size
            objects.append(obj);commands.append(list(map(str,cmd)))
        if not objects:raise RuntimeError('no sources '+group['name'])
        archive=dest/('lib'+group['name']+'.a');run([a.archiver,'rcsD',archive,*objects])
        report=dict(name=group['name'],kind=group['kind'],source_revision=group.get('revision'),
                    objects={o.name:sha(o) for o in objects},archive=str(archive),archive_sha256=sha(archive),
                    archive_bytes=archive.stat().st_size,arm64_ios17=True,automatic_initializers=False,
                    hardware_enabled=False,commands=commands)
        (dest/'build.json').write_text(json.dumps(report,indent=2)+'\n');return report
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:results=list(pool.map(build,manifest['groups']))
    result=dict(profile='arm64-apple-ios17.0',sdk=sdk,clang_version=run([clang,'--version']).splitlines()[0],
                source_manifest_sha256=sha(source/'manifest.json'),seconds=time.monotonic()-began,
                groups=results,device_operations=False,hardware_tested=False)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'profile':result['profile'],'seconds':result['seconds'],
                      'archives':[{k:r[k] for k in ['name','archive_bytes','archive_sha256']} for r in results],
                      'hardware_enabled':False},indent=2))
if __name__=='__main__':main()
