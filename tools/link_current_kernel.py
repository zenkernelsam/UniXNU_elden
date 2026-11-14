"""Link source-built base objects plus current units without historic artifacts."""
from pathlib import Path
import argparse,hashlib,io,json,os,plistlib,shutil,subprocess,sys,tarfile
from link_v91_kernel import native_identity,verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def main():
    if sys.flags.optimize:raise ValueError('Assertions must remain enabled')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--variant',choices=('bluetooth','gui'),default='bluetooth')
    for n in ('xnu-objects','native-archives','units','prelink-repository','original-kernelcache','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve()
    if out.exists():raise ValueError('Fresh output required')
    core=a.xnu_objects.resolve();aroot=a.native_archives.resolve();units=read(a.units/'result.json')
    profile=ROOT/'profiles'/('gui-build' if a.variant=='gui' else 'current-build')
    if not units['complete'] or units['profile_sha256']!=sha(profile/'plan.json'):raise ValueError('Incomplete current units')
    objects=read(core/'results.json');assert len(objects)==1010 and all(v['accepted'] and sha(Path(v['output']))==v['sha256'] for v in objects.values())
    plan=read(ROOT/'profiles/v91/image-build/plan.json');np=read(ROOT/'profiles/v91/native-build/plan.json');out.mkdir(parents=True)
    replacements={};bt=[];extra=[];core_replacements={}
    for name,row in units['units'].items():
        obj=Path(row['object']);assert row['accepted'] and sha(obj)==row['sha256'];dest=row['destination'];kind=dest['kind']
        if kind=='core':core_replacements[str(dest['index'])]=obj
        elif kind=='bluetooth':bt.append((obj,dest['member']))
        elif kind=='extra':extra.append(obj)
        else:replacements.setdefault(kind,{})[dest['member']]=obj
    archives={};ar_records={}
    for name,layout in np['archives'].items():
        old=aroot/name/layout['archive'];rec=read(old.parent/'result.json');assert sha(old)==rec['sha256']
        if name not in replacements:archives[name]=old;continue
        folder=out/'archives'/name;folder.mkdir(parents=True);names=[x for x in subprocess.check_output(['ar','t',old],text=True).splitlines() if not x.startswith('__.SYMDEF')]
        for member in replacements[name]:
            if member not in names:names.append(member)
        for member in names:
            data=replacements[name][member].read_bytes() if member in replacements[name] else subprocess.check_output(['ar','p',old,member]);(folder/member).write_bytes(data)
        target=folder/layout['archive'];subprocess.run(['ar','rcs',target,*names],cwd=folder,env=dict(os.environ,ZERO_AR_DATE='1'),check=True);archives[name]=target;ar_records[name]=names
    bluetooth=None
    if bt:
        folder=out/'archives/bluetooth';folder.mkdir(parents=True)
        for obj,name in bt:shutil.copyfile(obj,folder/name)
        bluetooth=folder/'libEldenBluetooth.a';subprocess.run(['ar','rcs',bluetooth,*[n for _,n in bt]],cwd=folder,env=dict(os.environ,ZERO_AR_DATE='1'),check=True)
    runtime=out/'runtime';runtime.mkdir();shutil.copyfile(archives['firehose'],runtime/'libfirehose_kernel.a')
    inputs={}
    for name,row in plan['link_inputs'].items():
        data=(ROOT/'profiles/v91/image-build'/row['path']).read_bytes()
        if row['encoding']=='bplist-preserve-order':data=plistlib.dumps(json.loads(data),fmt=plistlib.FMT_BINARY,sort_keys=False)
        assert hashlib.sha256(data).hexdigest()==row['sha256'];dest=out/name;dest.write_bytes(data);inputs[name]=dest
    paths={k:core_replacements.get(k,Path(v['output'])) for k,v in objects.items()}
    filelist=out/'link.filelist';filelist.write_text(''.join(str(paths[str(i)])+'\n' for i in plan['filelist_jobs']))
    clang=subprocess.check_output(['xcrun','--sdk','iphoneos','--find','clang++'],text=True).strip()
    def expand(v):
        import re
        v=v.replace('@CLANGXX@',clang).replace('@FILELIST@',str(filelist)).replace('@OUTPUT@',str(out/'native.macho')).replace('@RUNTIME@',str(runtime))
        v=re.sub(r'@OBJECT:([0-9]+)@',lambda m:str(paths[m[1]]),v);v=re.sub(r'@ARCHIVE:([^@]+)@',lambda m:str(archives[m[1]]),v);v=re.sub(r'@LINK_INPUT:([^@]+)@',lambda m:str(inputs[m[1]]),v)
        if '@' in v:raise ValueError('Unexpanded linker argument')
        return v
    cmd=[expand(v) for v in plan['link_command']]+(['-Wl,-force_load,'+str(bluetooth)] if bluetooth else [])+list(map(str,extra));write(out/'link-command.json',cmd)
    env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    with (out/'link.log').open('x') as f:subprocess.run(cmd,cwd=out,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
    sec,commands,sym=native_identity(out/'native.macho');expected=read(profile/'native-contract.json')
    if sec!=expected['sections'] or commands!=expected['commands'] or sym!=expected['symbols']:
        write(out/'native-difference.json',{'sections':sec,'commands':commands,'symbols':sym});raise ValueError('Current linked image contract differs')
    pre=plan['prelink'];assert sha(a.original_kernelcache)==pre['original_kernelcache_sha256'];kit=out/'prelink-tool';kit.mkdir()
    if a.variant=='gui':
        networking=read(ROOT/'profiles/v108-network-family/plan.json');pre['roots'].append(networking['added_component']);pre['original_executable_sha256'][networking['added_component']]=networking['executable_sha256']
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(a.prelink_repository),'archive',pre['commit']]))) as t:t.extractall(kit,filter='data')
    cmd=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(out/'native.macho'),'--prelinked',str(a.original_kernelcache.resolve()),'--slide='+pre['slide'],'--output',str(out/'kernel.macho')]
    for v in pre['roots']:cmd+=['--root',v]
    for v in pre['early_security_initializers']:cmd+=['--early-security-init',v]
    with (out/'prelink.log').open('x') as f:subprocess.run(cmd,cwd=out,stdout=f,stderr=subprocess.STDOUT,check=True)
    count=verify_original_executables(out/'kernel.macho',read(out/'kernel.json'),pre['original_executable_sha256'])
    write(out/'result.json',{'variant':a.variant,'profile_sha256':sha(profile/'plan.json'),'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),'native_contract_matches':True,'apple_execution_ranges':count,'core_objects':len(objects),'current_units':len(units['units']),'copied_historical_objects':0,'boot_packaged':False,'device_operation':False})
    print((out/'result.json').read_text())
if __name__=='__main__':main()
