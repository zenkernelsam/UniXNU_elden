"""Preprocess dependency lists and verify consumed Peach compile inputs."""
import argparse,gzip,hashlib,io,json,re,shlex,subprocess,tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(b):return hashlib.sha256(b).hexdigest()
def files(raw):
    with tarfile.open(fileobj=io.BytesIO(raw)) as t:return {m.name:t.extractfile(m).read() for m in t.getmembers() if m.isfile()}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('build','component-repository','linux-archive','backport','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();b=a.build.resolve();component=b/'component';out=a.output.resolve()
    if out.exists():raise ValueError('New audit directory required')
    result=json.loads((b/'result.json').read_text());lock=json.loads((ROOT/'drivers/EldenWifi/peach/linux-input.json').read_text())
    raw=gzip.decompress(a.linux_archive.read_bytes())
    if digest(raw)!=lock['tar_sha256']:raise ValueError('Linux archive changed')
    linux=files(raw);source=files(subprocess.check_output(['git','-c','core.autocrlf=false','-c','core.eol=lf','-C',str(a.component_repository),'archive',result['component_revision']]))
    backport=json.loads((a.backport/'result.json').read_text());patched={}
    for name,info in backport['files'].items():
        data=(a.backport/'tree'/name).read_bytes()
        if digest(data)!=info['output_sha256']:raise ValueError('Backport changed')
        patched[name]=data
    prefix='drivers/net/wireless/ath/ath12k/';traces=set()
    for name,data in linux.items():
        relative=name.removeprefix(prefix)
        if name.startswith(prefix) and Path(name).suffix in ('.c','.h') and Path(name).name!='trace.h' and (len(Path(relative).parts)==1 or Path(relative).parts[0]=='wifi7'):
            traces.update(re.findall(r'trace_ath12k_[a-z_0-9]+',patched.get(name,data).decode()))
    generated={'out/ath12k-src/trace.h':('#ifndef LX_ATH12K_TRACE_H\n#define LX_ATH12K_TRACE_H\n'+''.join('#define '+n+'(...) ((void)0)\n' for n in sorted(traces))+'#endif\n').encode()}
    text=linux['include/linux/netdevice.h'].decode();enums=[]
    for name in ('netdev_priv_flags','netdev_stat_type','netdev_cmd'):
        m=re.search(r'enum '+name+r'\s*\{.*?\};',text,re.S)
        if not m:raise ValueError('Expected netdevice enum')
        enums.append(m[0])
    generated['out/pinned/linux/netdevice_enums.h']=('/* SPDX-License-Identifier: GPL-2.0 */\n#ifndef NW_NETDEVICE_ENUMS_H\n#define NW_NETDEVICE_ENUMS_H\n'+'\n'.join(enums)+'\n#endif\n').encode()
    out.mkdir(parents=True);records={};units={}
    for name,record in result['objects'].items():
        command=list(record['command']);command.remove('-c');i=command.index('-o');del command[i:i+2]
        resources=[Path(command[i+1]).resolve() for i,v in enumerate(command[:-1]) if v=='-isystem' and '/lib/clang/' in command[i+1]]
        if len(resources)!=1:raise ValueError('Unqualified compiler resource path')
        dependency=out/(name.replace('/','-')+'.d')
        subprocess.run(command+['-M','-MF',str(dependency)],capture_output=True,check=True)
        body=dependency.read_text().replace('\\\n',' ').split(':',1)[1];paths=shlex.split(body);units[name]=len(paths)
        for v in paths:
            path=Path(v).resolve(strict=True);key=str(path)
            if key in records:continue
            actual=path.read_bytes();expected=None;kind=None;relative=None
            if path.is_relative_to(component/'deps/linux'):
                relative=path.relative_to(component/'deps/linux').as_posix();expected=linux[relative];kind='linux'
            elif path.is_relative_to(component):
                relative=path.relative_to(component).as_posix()
                if relative in generated:expected=generated[relative];kind='generated'
                elif relative.startswith('out/ath12k-src/'):
                    n=prefix+relative.removeprefix('out/ath12k-src/');expected=patched.get(n,linux.get(n));kind='backport' if n in patched else 'linux-copy'
                elif relative.startswith('out/pinned/'):
                    n='include/'+relative.removeprefix('out/pinned/');expected=patched.get(n);kind='backport'
                else:expected=source.get(relative);kind='component'
            elif path.is_relative_to(resources[0]):kind='compiler-header';relative=path.relative_to(resources[0]).as_posix()
            if kind!='compiler-header' and (expected is None or actual!=expected):raise ValueError('Consumed source differs: '+key)
            if kind is None:raise ValueError('Unknown dependency: '+key)
            records[key]={'kind':kind,'relative':relative,'sha256':digest(actual)}
    counts={kind:sum(r['kind']==kind for r in records.values()) for kind in sorted({r['kind'] for r in records.values()})}
    report={'passed':True,'unit_dependency_counts':units,'kinds':counts,'files':records,'compiler_headers_recorded_not_external_reference_matched':True,'device_operations':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'verified_files':len(records),'kinds':counts}))
if __name__=='__main__':main()
