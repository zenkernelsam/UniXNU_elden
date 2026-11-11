#!/usr/bin/env python3
"""Recompile captured XNU commands in an isolated source-only staging tree.

The capture is an audit input, not yet the portable final build profile. No old
object/archive is copied into staging; optional comparisons only read originals.
"""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,os,re,shlex,struct,subprocess,threading,time

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def sections(p):
    b=p.read_bytes()
    if len(b)<32 or struct.unpack_from('<4I',b)!=(0xfeedfacf,0x100000c,0,1):raise ValueError('Expected ordinary ARM64 Mach-O object: '+str(p))
    at=32;result={}
    for _ in range(struct.unpack_from('<I',b,16)[0]):
        cmd,length=struct.unpack_from('<II',b,at)
        if cmd==0x19:
            for j in range(struct.unpack_from('<I',b,at+64)[0]):
                x=at+72+80*j;name=b[x:x+16].split(b'\0')[0].decode();seg=b[x+16:x+32].split(b'\0')[0].decode()
                if seg=='__DWARF':continue
                address,size,off=struct.unpack_from('<QQI',b,x+32);flags=struct.unpack_from('<I',b,x+64)[0]
                data=b'' if flags&255 in (1,12,18) else b[off:off+size]
                # Object-local addresses of zero-fill sections can move with
                # DWARF size; compare bytes, extent and kind before final link.
                result[seg+'/'+name]=[size,flags,hashlib.sha256(data).hexdigest()]
        at+=length
    return result

def global_symbols(p):
    """Normalize exported/undefined/absolute bindings, excluding debug stabs."""
    b=p.read_bytes();at=32;section_rows=[];symtab=None
    if len(b)<32 or struct.unpack_from('<I',b)[0]!=0xfeedfacf:raise ValueError('Expected Mach-O')
    for _ in range(struct.unpack_from('<I',b,16)[0]):
        cmd,length=struct.unpack_from('<II',b,at)
        if cmd==0x19:
            for j in range(struct.unpack_from('<I',b,at+64)[0]):
                x=at+72+j*80;name=b[x:x+16].split(b'\0')[0].decode();seg=b[x+16:x+32].split(b'\0')[0].decode()
                section_rows.append((seg+'/'+name,struct.unpack_from('<Q',b,x+32)[0]))
        elif cmd==2:symtab=struct.unpack_from('<4I',b,at+8)
        at+=length
    if symtab is None:return {}
    offset,count,strings,_=symtab;out={}
    def string(i):return b[strings+i:b.index(b'\0',strings+i)].decode()
    for i in range(count):
        name,kind,section,desc,value=struct.unpack_from('<IBBHQ',b,offset+i*16)
        if kind&0xe0 or not kind&1:continue
        key=string(name);target='-'
        if kind&0x0e==0x0e:target,base=section_rows[section-1];value-=base
        elif kind&0x0e==0x0a:value=string(value)
        out[key]=[kind,target,value,desc]
    return out

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True);p.add_argument('--historical-root',required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--payload-header',type=Path,required=True)
    p.add_argument('--nvt-header',type=Path,help='Regenerated stock NT36536 header when the capture uses it')
    p.add_argument('--only',help='Comma-separated job indices for initial validation')
    p.add_argument('--jobs',type=int,default=4,choices=range(1,9));p.add_argument('--resume',action='store_true')
    p.add_argument('--compare-old',action='store_true',help='Read original objects only to compare loaded sections')
    p.add_argument('--xnu-repository',type=Path,required=True,help='Repository containing the pinned XNU host-tool sources')
    p.add_argument('--llvm-objcopy',type=Path,help='LLVM objcopy for recorded late-initializer transforms')
    a=p.parse_args();capture=json.loads(a.capture.read_text());out=a.output.resolve();old=a.historical_root.rstrip('/')
    if capture['missing'] or capture['no_dependency_records']:raise RuntimeError('Capture has incomplete dependencies')
    stamp=sha(a.capture)
    if out.exists():
        if not a.resume or (out/'capture.sha256').read_text().strip()!=stamp:raise RuntimeError('Output must be new, or explicitly resume the same capture')
    else:out.mkdir(parents=True);(out/'capture.sha256').write_text(stamp+'\n')
    project=out/'project';project.mkdir(exist_ok=True)
    def virtual(name):
        if not name.startswith(old+'/'):raise ValueError('Input outside historical project: '+name)
        relative=name[len(old)+1:]
        if relative.startswith('research/'):relative='records/'+relative[len('research/'):]
        q=(project/relative).resolve()
        if not q.is_relative_to(project):raise ValueError('Input escapes output')
        return q
    def remap(arg):
        return re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),arg)
    if remap(old+'/research/example')!=str(project/'records/example'):raise RuntimeError('Nested-root path remapping failed')
    count=0
    for name,item in capture['files'].items():
        source=a.payload_header if name.endswith('/EldenIconCpuPayload.h') else Path(item['path'])
        if name.endswith('/EldenNvtFirmware.h'):
            if not a.nvt_header:raise RuntimeError('Regenerate and supply --nvt-header')
            source=a.nvt_header
        if sha(source)!=item['sha256']:raise RuntimeError('Input SHA mismatch: '+str(source))
        data=source.read_bytes()
        if source.suffix in ('.o','.cpo','.a','.macho','.bin','.img') or b'\0' in data:raise RuntimeError('Compiled/binary input forbidden: '+str(source))
        dest=virtual(name);dest.parent.mkdir(parents=True,exist_ok=True)
        if dest.exists():
            if sha(dest)!=item['sha256']:raise RuntimeError('Staged source changed: '+str(dest))
        else:dest.write_bytes(data)
        count+=1
    for name,item in capture['toolchain_files'].items():
        if sha(Path(item.get('path',name)))!=item['sha256']:raise RuntimeError('Toolchain header mismatch: '+name)
    # Preserve the compiler's original source spelling, including __FILE__.
    # Late prefix substitution alone can change LLVM's string-pool ordering.
    entries=[]
    for name in capture['files']:
        local=str(virtual(name))
        entries.extend([{'type':'file','name':name,'external-contents':local},
                        {'type':'file','name':local,'external-contents':local}])
    for name,item in capture['toolchain_files'].items():
        actual=item.get('path',name)
        entries.append({'type':'file','name':name,'external-contents':actual})
        if actual!=name:entries.append({'type':'file','name':actual,'external-contents':actual})
    overlay={'version':0,'case-sensitive':True,'use-external-names':False,
             'fallthrough':False,'roots':entries}
    overlay_path=out/'source-overlay.json';overlay_path.write_text(json.dumps(overlay,indent=2)+'\n')
    print(json.dumps({'source_inputs':count,'copied_old_objects':0,'output':str(out)}),flush=True)
    objects=out/'objects';objects.mkdir(exist_ok=True);logs=out/'logs';logs.mkdir(exist_ok=True)
    results=json.loads((out/'results.json').read_text()) if (out/'results.json').exists() else {}
    indices=list(map(int,a.only.split(','))) if a.only else list(range(len(capture['jobs'])))
    if any(i<0 or i>=len(capture['jobs']) for i in indices):raise ValueError('Invalid job index')
    stop=threading.Event();started=time.monotonic()
    runner_sha256=sha(Path(__file__))
    pin='d8b80295118ef25ac3a784134bcf95cd8e88109f'
    def git_source(name):return subprocess.check_output(['git','-C',str(a.xnu_repository),'show',pin+':'+name])
    arm_make=git_source('osfmk/conf/Makefile.arm64').decode()
    hib_names=set(re.findall(r'\b[\w]+\.o\b',arm_make.split('UNCONFIGURED_HIB_FILES=',1)[1].split('\n\n',1)[0]))
    libsa_make=git_source('libsa/conf/Makefile.template').decode()
    if 'KLD_FILES = $(OBJS)' not in libsa_make:raise RuntimeError('Unexpected libsa segment rules')
    host=out/'host-tools';host.mkdir(exist_ok=True);host_source=host/'setsegname.c';source_data=git_source('SETUP/setsegname/setsegname.c')
    if host_source.exists() and host_source.read_bytes()!=source_data:raise RuntimeError('Pinned host-tool source changed')
    host_source.write_bytes(source_data);segment_tool=host/'setsegname'
    if not segment_tool.exists():
        with (host/'compile.log').open('x') as log:
            subprocess.run(['xcrun','--sdk','macosx','clang','-O2',str(host_source),'-o',str(segment_tool)],stdout=log,stderr=subprocess.STDOUT,check=True)
        (host/'build.json').write_text(json.dumps({'source_sha256':sha(host_source),'binary_sha256':sha(segment_tool),'xnu_commit':pin},indent=2)+'\n')
    host_record=json.loads((host/'build.json').read_text())
    if sha(segment_tool)!=host_record['binary_sha256'] or sha(host_source)!=host_record['source_sha256']:raise RuntimeError('Host-tool identity mismatch')
    def normalized(command):
        result=[];i=0
        while i<len(command):
            if command[i]=='-o':result+=['-o','OUTPUT'];i+=2
            else:result.append(command[i]);i+=1
        return result
    def build(i):
        if stop.is_set():return i,{'cancelled':True}
        rec=capture['jobs'][i]
        name=Path(rec['original_object']).name;post=[]
        if name in hib_names and '/osfmk/' in rec['original_object']:post=[['-n','__HIB']]
        if '/libsa/DEVELOPMENT/' in rec['original_object']:post=[['-s','__TEXT','-n','__KLD'],['-i','__KLD','-n','__KLDDATA']]
        if name=='lastkernelconstructor.o':post=[['-s','__DATA','-n','__LASTDATA_CONST']]
        prior=results.get(str(i))
        if prior and prior.get('accepted') and prior.get('runner_sha256')==runner_sha256 and (not a.compare_old or prior.get('global_symbols_equal')):
            previous=Path(prior['output'])
            if previous.exists() and sha(previous)==prior.get('sha256'):return i,dict(prior,reused=True)
        attempt=len(list(logs.glob(str(i)+'*.log')))+1
        obj=objects/(str(i)+'-attempt'+str(attempt)+Path(rec['original_object']).suffix)
        cwd=virtual(rec['directory']);cwd.mkdir(parents=True,exist_ok=True)
        cmd=[];j=0
        while j<len(rec['arguments']):
            v=rec['arguments'][j]
            if v in ('-o','-MF','-MT','-MQ','-MJ','-serialize-diagnostics'):j+=2;continue
            if v in ('-MD','-MMD','-MP'):j+=1;continue
            cmd.append(remap(v));j+=1
        compiler=Path(cmd[0]).name
        if compiler=='xcrun':
            if cmd[1:4] not in (['--sdk','iphoneos','clang'],['--sdk','iphoneos','clang++']) and cmd[1:2] not in (['clang'],['clang++']):
                raise RuntimeError('Unexpected xcrun invocation')
        elif compiler not in ('clang','clang++'):raise RuntimeError('Unexpected compiler executable')
        cmd+=['-o',str(obj),'-fdebug-compilation-dir='+rec['directory']]
        approved_env=capture.get('environment',{})
        if set(approved_env)-{'SDKROOT'}:raise RuntimeError('Unsupported captured environment keys')
        env=dict(os.environ,**approved_env,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option')
        if Path(rec['file']).suffix.lower()=='.s':
            cmd+=['-ffile-prefix-map='+str(project)+'='+old,
                  '-ffile-prefix-map='+str(project)+'/records='+old+'/research']
        else:
            dry=subprocess.run(cmd+['-###'],cwd=cwd,env=env,capture_output=True,text=True,check=True)
            invocations=[shlex.split(line.strip()) for line in dry.stderr.splitlines() if '"-cc1"' in line]
            if len(invocations)!=1:raise RuntimeError('Expected one compiler frontend invocation')
            def logical(v):
                return re.sub(re.escape(str(project))+r'/(records/)?',lambda m:old+('/research/' if m[1] else '/'),v)
            cmd=[logical(v) for v in invocations[0]]+['-ivfsoverlay',str(overlay_path)]
        if prior and prior.get('accepted') and prior.get('postprocess',[])==post and prior.get('objcopy',[])==rec.get('objcopy',[]) and normalized(prior['command'])==normalized(cmd):
            previous=Path(prior['output'])
            if previous.exists() and sha(previous)==prior.get('sha256') and (not a.compare_old or (prior.get('loaded_sections_equal') and prior.get('global_symbols_equal'))):
                return i,dict(prior,reused=True,runner_sha256=runner_sha256)
        logpath=logs/(str(i)+'-attempt'+str(attempt)+'.log')
        with logpath.open('x') as log:
            built=subprocess.run(cmd,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        result={'source':rec['file'],'output':str(obj),'returncode':built.returncode,'accepted':False,'command':cmd,'log':str(logpath),'runner_sha256':runner_sha256}
        if built.returncode:stop.set();return i,result
        result['postprocess']=post
        for step_index,step in enumerate(post):
            transformed=obj.with_name(obj.stem+'-segments'+str(step_index)+obj.suffix)
            subprocess.run([str(segment_tool),*step,'-o',str(transformed),str(obj)],check=True,capture_output=True)
            obj=transformed;result['output']=str(obj)
        flags=rec.get('objcopy',[]);result['objcopy']=flags
        if flags:
            if not a.llvm_objcopy or flags[0]!='--remove-section=__DATA,__mod_init_func' or any(not v.startswith('--globalize-symbol=__GLOBAL__sub_I_') for v in flags[1:]):raise ValueError('Unsupported objcopy transform')
            transformed=obj.with_name(obj.stem+'-late'+obj.suffix)
            subprocess.run([str(a.llvm_objcopy),*flags,str(obj),str(transformed)],check=True,capture_output=True)
            obj=transformed;result['output']=str(obj)
        result['sha256']=sha(obj);result['accepted']=True
        if a.compare_old or 'expected_sections' in rec:
            left=sections(obj);right=rec['expected_sections'] if 'expected_sections' in rec else sections(Path(rec['original_object']))
            result['loaded_sections_equal']=left==right
            result['different_sections']=[k for k in sorted(set(left)|set(right)) if left.get(k)!=right.get(k)]
            result['accepted']=left==right
            if not result['accepted']:stop.set()
        if a.compare_old or 'expected_symbols' in rec:
            expected=rec['expected_symbols'] if 'expected_symbols' in rec else global_symbols(Path(rec['original_object']))
            result['global_symbols_equal']=global_symbols(obj)==expected
            result['accepted']=result['accepted'] and result['global_symbols_equal']
            if not result['accepted']:stop.set()
        return i,result
    completed=0
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        futures=[pool.submit(build,i) for i in indices]
        for future in concurrent.futures.as_completed(futures):
            i,result=future.result()
            if result.get('cancelled'):continue
            results[str(i)]=result;completed+=1
            (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
            if completed%50==0 or not result['accepted'] or len(indices)<=3:
                print(json.dumps({'completed':completed,'selected':len(indices),'job':i,'accepted':result['accepted'],'differences':result.get('different_sections'),'seconds':round(time.monotonic()-started,1)}),flush=True)
    accepted=sum(results.get(str(i),{}).get('accepted',False) for i in indices)
    report={'selected':len(indices),'accepted':accepted,'copied_old_objects':0,'source_files':count,'seconds':time.monotonic()-started,'complete_object_build':accepted==len(capture['jobs']),'native_linked':False,'device_operation':False}
    (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2),flush=True)
    if accepted!=len(indices):raise SystemExit(1)

if __name__=='__main__':main()
