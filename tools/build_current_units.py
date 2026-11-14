"""Build the current thirty changed units from source providers, never old objects.

Base capture providers are produced from this checkout's v91 source manifests;
their object outputs are not consulted. Historical file names are VFS keys only.
"""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,os,re,subprocess,sys
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1];PROFILE=ROOT/'profiles/current-build'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def main():
    global PROFILE
    if sys.flags.optimize:raise ValueError('Assertions must remain enabled')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--variant',choices=('bluetooth','gui'),default='bluetooth')
    for n in ('xnu-source','dependencies','icon-header','nvt-header','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--generated',type=Path)
    p.add_argument('--jobs',type=int,default=4,choices=range(1,9));p.add_argument('--only',help='Comma-separated names for preflight only')
    a=p.parse_args();PROFILE=ROOT/'profiles'/('gui-build' if a.variant=='gui' else 'current-build');plan=read(PROFILE/'plan.json');out=a.output.resolve()
    if out.exists():raise ValueError('Fresh output required')
    baseplans={'xnu':read(ROOT/'profiles/v91/object-build/plan.json'),'native':read(ROOT/'profiles/v91/native-build/plan.json')};old=plan['virtual_root']
    clang=subprocess.check_output(['xcrun','--sdk','iphoneos','--find','clang'],text=True).strip()
    sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
    resource=subprocess.check_output([clang,'-print-resource-dir'],text=True).strip()
    def toolpath(path):
        if '/usr/lib/clang/17/' in path:return resource+'/'+path.split('/usr/lib/clang/17/',1)[1]
        if '.sdk/' in path:return sdk+'/'+path.split('.sdk/',1)[1]
        raise ValueError('Unrecognized toolchain provider '+path)
    out.mkdir(parents=True);store=out/'source';store.mkdir();generated=read(a.generated/'providers.json') if a.generated else {};provided={};input_audit={}
    for digest,desc in plan['providers'].items():
        kind=desc['kind']
        if kind=='base-provider':
            label=desc['profile'];bp=baseplans[label];item=bp['providers'][desc['key']];base=ROOT/'profiles/v91'/('object-build' if label=='xnu' else 'native-build')
            if item['kind']=='reconstructed-xnu':source=a.xnu_source/item['path']
            elif item['kind']=='profile-source':source=base/item['path']
            elif item['kind']=='repository':source=ROOT/item['path']
            elif item['kind']=='external-generated-icon-payload':source=a.icon_header
            elif item['kind']=='external-nvt':source=a.nvt_header
            elif item['kind']=='dependency':
                repo=a.dependencies/item['dependency'];pin=bp['dependencies'][item['dependency']]['commit']
                data=subprocess.check_output(['git','-C',str(repo),'show',pin+':'+item['path']]);source=store/(digest+'.input');source.write_bytes(data)
            else:raise ValueError('Unknown base source kind '+item['kind'])
        elif kind=='repository':source=ROOT/desc['path']
        elif kind=='snapshot':source=PROFILE/desc['path']
        elif kind=='generated':
            if not a.generated:raise ValueError('This profile needs --generated')
            source=a.generated/generated[digest]
        elif kind=='toolchain':source=Path(toolpath(desc['path']))
        else:raise ValueError('Unknown provider '+kind)
        if sha(source)!=digest:raise ValueError('Source identity differs '+str(source))
        if source.suffix in ('.o','.a','.macho','.img','.bin'):raise ValueError('Binary build input forbidden '+str(source))
        data=source.read_bytes()
        if b'\0' in data:raise ValueError('Non-source bytes '+str(source))
        dest=store/digest;dest.write_bytes(data);provided[digest]=dest
        input_audit[digest]={'kind':kind,'input':str(source),'staged':str(dest),'bytes':len(data)}
    write(out/'inputs.json',input_audit);objects=out/'objects';objects.mkdir();logs=out/'logs';logs.mkdir()
    selected=[j for j in plan['jobs'] if not a.only or j['name'] in a.only.split(',')]
    if a.only and len(selected)!=len(a.only.split(',')):raise ValueError('Unknown selected unit')
    def compile_one(job):
        name=job['name'];mapping=dict(plan.get('common_mapping',{}),**plan['mappings'][job['mapping']]);cwd=out/'working'/name;cwd.mkdir(parents=True)
        virtual_cwd=job['virtual_cwd'].replace('@VROOT@',old)
        entries=[]
        for key,digest in mapping.items():
            key=key.replace('@VROOT@',old);source=provided[digest]
            entries.append({'type':'file','name':key,'external-contents':str(source)})
            if key.startswith(virtual_cwd+'/'):
                local=cwd/key[len(virtual_cwd)+1:];local.parent.mkdir(parents=True,exist_ok=True)
                # Actual source copies only, needed for forced relative includes.
                local.write_bytes(source.read_bytes());entries.append({'type':'file','name':str(local),'external-contents':str(source)})
        overlay=out/(name+'-overlay.json');write(overlay,{'version':0,'case-sensitive':True,'use-external-names':False,'fallthrough':False,'roots':entries})
        command=[x.replace('@VROOT@',old) for x in job['command']];command[0]=clang
        if '-cc1' not in command:
            # The Clang driver checks assembler inputs before invoking the VFS
            # frontend. Give it a real file in this run; preserve logical names
            # for preprocessor/debug strings, with all includes still closed.
            assembly=[(i,x) for i,x in enumerate(command) if x.endswith(('.s','.S')) and x.replace(old,'@VROOT@') in mapping]
            if len(assembly)!=1:raise ValueError('Expected one assembly source in driver command')
            i,virtual_source=assembly[0];local_source=cwd/Path(virtual_source).name
            local_source.write_bytes(provided[mapping[virtual_source.replace(old,'@VROOT@')]].read_bytes())
            entries.append({'type':'file','name':str(local_source),'external-contents':str(local_source)})
            write(overlay,{'version':0,'case-sensitive':True,'use-external-names':False,'fallthrough':False,'roots':entries})
            command[i]=str(local_source);command+=['-ffile-prefix-map='+str(local_source)+'='+virtual_source]
        # Toolchain is selected by xcrun; all logical include spellings still
        # resolve through the complete closed overlay, not those actual paths.
        if '-resource-dir' in command:command[command.index('-resource-dir')+1]=resource
        obj=objects/(name+'.o');command[command.index('-o')+1]=str(obj);command+=['-ivfsoverlay',str(overlay)]
        if '@OUTPUT@' in command:raise ValueError('Unexpanded output')
        env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
        result=subprocess.run(command,cwd=cwd,env=env,capture_output=True)
        (logs/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise ValueError(name+' compile failed: '+result.stderr.decode(errors='replace')[-1500:])
        if job['post']:
            final=objects/(name+'.late.o');subprocess.run([str(a.llvm/'llvm-objcopy'),*job['post'],str(obj),str(final)],check=True);obj=final
        actual=sections(obj);symbols=global_symbols(obj)
        if actual!=job['expected_sections'] or symbols!=job['expected_symbols']:
            write(logs/(name+'-difference.json'),{'actual_sections':actual,'expected_sections':job['expected_sections'],'actual_symbols':symbols,'expected_symbols':job['expected_symbols']})
            raise ValueError(name+' compiled identity differs')
        row={'object':str(obj),'sha256':sha(obj),'destination':job['destination'],'accepted':True,'command':command,'closed_overlay':True}
        print(name+' accepted',flush=True);return name,row
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:results=dict(pool.map(compile_one,selected))
    write(out/'result.json',{'variant':a.variant,'profile_sha256':sha(PROFILE/'plan.json'),'units':results,'copied_old_objects':0,'complete':len(results)==len(plan['jobs']),'device_operation':False})
if __name__=='__main__':main()
