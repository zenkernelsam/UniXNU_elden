"""Rebuild the frozen WCN stack, wire startup/stop into v94, and prelink offline.

No boot signing or device access. The resulting kernel keeps radio options off
until a separately reviewed hardware profile explicitly selects a mode.
"""
from pathlib import Path
import argparse,copy,hashlib,io,json,os,re,shutil,subprocess,sys,tarfile
from concurrent.futures import ThreadPoolExecutor
from build_fbwc_kernel import verified_objects
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
PROFILE=ROOT/'profiles/v97-wcn'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,value):p.write_text(json.dumps(value,indent=2)+'\n')
def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0x0e!=0}

def main():
    global PROFILE
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--profile',choices=('v97-wcn','v98-bt-bootstrap','v99-bt-runtime','v101-bt-clock','v102-bt-wake'),default='v97-wcn')
    p.add_argument('--runtime',type=Path,help='Verified BlueTool-only runtime input for v99')
    for n in ('v94-build','dependency','firmware','xnu-source','prelink-repository','original-kernelcache','llvm','output'):
        p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();PROFILE=ROOT/'profiles'/a.profile;plan=read(PROFILE/'plan.json');base=a.v94_build.resolve();out=a.output.resolve()
    if out.exists():raise ValueError('Output must be new')
    if sha(base/'kernel.macho')!=plan['baseline_kernel_sha256'] or sha(base/'native.macho')!=plan['baseline_native_sha256']:
        raise ValueError('Not the qualified v94 baseline')
    if sha(ROOT/'profiles/v94-fbwc-observation/plan.json')!=plan['baseline_profile_sha256']:raise ValueError('Baseline profile changed')
    inputs=read(base/'inputs.json');bp=read(ROOT/'profiles/v94-fbwc-observation/plan.json')
    cc,cr=verified_objects(Path(inputs['core']),bp['parent_core_profile'])
    nc,nr=verified_objects(Path(inputs['native']),bp['parent_native_profile'])
    ip=read(ROOT/'profiles/v91/image-build/plan.json');image=Path(inputs['image'])
    if sha(ROOT/'profiles/v91/image-build/plan.json')!=bp['parent_image_profile']:raise ValueError('Image profile changed')
    if sha(a.original_kernelcache)!=ip['prelink']['original_kernelcache_sha256']:raise ValueError('Wrong original kernelcache')
    out.mkdir(parents=True);source=out/'source';source.mkdir();records={}
    for name,item in plan['owned'].items():
        original=ROOT/item['path']
        if sha(original)!=item['sha256']:raise ValueError('Owned source changed: '+name)
        shutil.copyfile(original,source/name);records[name]=item['sha256']
    if a.profile in ('v99-bt-runtime','v101-bt-clock','v102-bt-wake'):
        if not a.runtime:raise ValueError('v99 requires its runtime input')
        runtime=read(a.runtime/'report.json')
        if runtime['helper_sha256']!=plan['runtime_helper_sha256'] or sha(a.runtime/'EldenBtRuntimePayload.h')!=plan['runtime_header_sha256'] or sha(a.runtime/'runtime.trustcache')!=plan['runtime_trust_sha256']:
            raise ValueError('Runtime payload identity differs')
        shutil.copyfile(a.runtime/'EldenBtRuntimePayload.h',source/'EldenBtRuntimePayload.h')
        shutil.copyfile(source/'Runtime.inc',source/'EldenBtRuntime.inc')
        shutil.copyfile(source/'TextGuard.h',source/'EldenBtTextGuard.h')
    for name,item in plan['dependency']['files'].items():
        data=subprocess.check_output(['git','-C',str(a.dependency),'show',plan['dependency']['revision']+':'+item['path']])
        if hashlib.sha256(data).hexdigest()!=item['sha256']:raise ValueError('Dependency changed: '+name)
        (source/name).write_bytes(data);records[name]=item['sha256']
    sys.path.insert(0,str(source));from BridgeSource import prepare
    c,h=prepare((source/'BtBridge.c').read_bytes(),(source/'BtBridge.h').read_bytes())
    (source/'BtBridge.c').write_bytes(c);(source/'BtBridge.h').write_bytes(h)
    rpm=(source/'Sm8750Rpmh.c').read_text()
    old='    rc = transact(&ctx, cmds, n, wait_mask);\n    Sm8750RpmhDrv2Release();'
    if rpm.count(old)!=1:raise ValueError('RPMh ownership fence changed')
    rpm=rpm.replace(old,'    rc = transact(&ctx, cmds, n, wait_mask);\n    if (!rc || result->step < Sm8750RpmhStepTrigger) Sm8750RpmhDrv2Release();')
    (source/'Sm8750Rpmh.c').write_text(rpm,newline='\n')
    if sha(source/'Sm8750Rpmh.c')!='8d4d7196b922512633ec37915fee24a96a735c5b9892de400055ab7c3c96f565':raise ValueError('RPMh fence does not match prior qualification')
    names={'brhbtfw20.tlv':'elden_wcn_bt_patch','brhbtnv20.bin':'elden_wcn_bt_nvm','brhperifw20.tlv':'elden_wcn_patch','brhperinv20.bin':'elden_wcn_nvm','tmel_peach_20.elf':'elden_wcn_tmel'}
    header='#include <stdint.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\n';definitions='#include <stdint.h>\n'
    for item in plan['firmware']:
        path=a.firmware/item['file'];data=path.read_bytes()
        if len(data)!=item['bytes'] or sha(path)!=item['sha256']:raise ValueError('Firmware mismatch')
        name=names[path.name];header+=f'extern const uint8_t {name}[{len(data)}];\n'
        definitions+='const uint8_t '+name+'[]={\n'+','.join(str(v) for v in data)+'\n};\n'
    (source/'EldenWcnFirmware.h').write_text(header+'#ifdef __cplusplus\n}\n#endif\n')
    (source/'EldenWcnFirmware.c').write_text(definitions)
    if sha(PROFILE/'EldenSdamRecovery.inc')!=plan['recovery_sha256']:raise ValueError('Recovery delta changed')
    obj=out/'objects';obj.mkdir();logs=out/'logs';logs.mkdir();audit={}
    clang=subprocess.check_output(['xcrun','--sdk','iphoneos','--find','clang'],text=True).strip()
    sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
    def inspect(name,target,command,prior=None):
        asm=subprocess.check_output([str(a.llvm/'llvm-objdump'),'--disassemble','--reloc',str(target)],text=True)
        (logs/(name+'.asm')).write_text(asm)
        ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',asm,re.M)
        if not ops and name!='EldenWcnFirmware':raise ValueError('Missing instructions: '+name)
        if any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',s) for s in ops):raise ValueError('FP/SIMD in '+name)
        secs=sections(target)
        init=[n for n in secs if n.endswith('/__mod_init_func')]
        if init:
            previous=sections(prior) if prior else {}
            if any(n not in previous or secs[n][:2]!=previous[n][:2] for n in init):
                raise ValueError('New automatic initializer in '+name)
            def init_relocs(path):
                reloc=subprocess.check_output([str(a.llvm/'llvm-objdump'),'--reloc',str(path)],text=True)
                block=re.search(r'RELOCATION RECORDS FOR \[__mod_init_func\]:\n(.*?)(?:\n\n|\Z)',reloc,re.S)
                if not block:raise ValueError('Initializer relocations missing')
                return block[1]
            if init_relocs(target)!=init_relocs(prior):raise ValueError('Initializer target changed')
        return {'file':str(target),'sha256':sha(target),'source_sha256':sha(source/(name+'.c')) if (source/(name+'.c')).exists() else None,
                'instructions':len(ops),'fp_simd':False,'command':command,'exports':sorted(exports(target))}
    def compile_c(filename):
        name=Path(filename).stem;target=obj/(name+'.o')
        cmd=[clang,'-target','arm64-apple-ios17.0','-isysroot',sdk,'-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-common','-mgeneral-regs-only','-DKERNEL','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I',str(source),'-c',str(source/filename),'-o',str(target)]
        result=subprocess.run(cmd,capture_output=True,text=True);(logs/(name+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:raise ValueError(name+' failed: '+result.stderr[-2000:])
        return name,inspect(name,target,cmd)
    with ThreadPoolExecutor(max_workers=4) as pool:
        for name,item in pool.map(compile_c,plan['c_units']):audit[name]=item
    extra=out/'extra-headers';extra.mkdir();added_headers={}
    def compile_cpp(name,prior,cap,build,filename=None,overrides=None,defines=(),source_override=None,scalar=False):
        if sha(Path(prior['output']))!=prior['sha256']:raise ValueError('Parent object changed')
        command=list(prior['command']);overlay=read(Path(command[command.index('-ivfsoverlay')+1]))
        if overlay.get('fallthrough') is not False:raise ValueError('Expected closed compiler input overlay')
        if filename:
            command=[str(source/filename) if x==prior['source'] else x for x in command]
            if '-main-file-name' in command:command[command.index('-main-file-name')+1]=filename
        if source_override:
            selected=[x for x in overlay['roots'] if x['name']==prior['source']]
            if len(selected)!=1:raise ValueError('Core source overlay is ambiguous')
            old_file=selected[0]['external-contents']
            if sha(Path(old_file))!=cap['files'][prior['source']]['sha256']:raise ValueError('Core parent changed')
            for entry in overlay['roots']:
                if entry['external-contents']==old_file:entry['external-contents']=str(source_override)
        for key,path in (overrides or {}).items():
            matched=0
            for entry in overlay['roots']:
                if entry['name'].endswith('/'+key):
                    if sha(Path(entry['external-contents']))!=plan['recovery_parent_sha256']:raise ValueError('Recovery parent changed')
                    entry['external-contents']=str(path);matched+=1
            if not matched:raise ValueError('Source override did not bind: '+key)
        overlay['roots'] += [{'type':'file','name':str(p),'external-contents':str(p)} for p in source.iterdir() if p.is_file()]
        overlay['roots'] += [{'type':'file','name':str(extra/n),'external-contents':str(extra/n)} for n in added_headers]
        vfs=out/(name+'-overlay.json');write(vfs,overlay);command[command.index('-ivfsoverlay')+1]=str(vfs)
        target=obj/(name+'.o');command[command.index('-o')+1]=str(target)
        command[2:2]=['-I',str(source),'-I',str(extra)]+sum((['-D',v+'=1'] for v in defines),[])
        if scalar:command+=['-target-feature','-neon','-target-feature','-fp-armv8']
        job=next(j for j in cap['jobs'] if j['file']==prior['source']);old=bp['virtual_root']
        rel=Path(job['directory']).relative_to(old).as_posix()
        if rel.startswith('research/'):rel='records/'+rel[9:]
        cwd=(build/'project'/rel).resolve(strict=True)
        for attempt in range(128):
            result=subprocess.run(command,cwd=cwd,capture_output=True,text=True)
            (logs/(name+'.'+str(attempt)+'.log')).write_text(result.stdout+result.stderr)
            if not result.returncode:break
            missing=re.findall(r"fatal error: '([^']+)' file not found",result.stderr)
            if len(missing)!=1 or '..' in Path(missing[0]).parts or missing[0] in added_headers:break
            relative=missing[0];matches=[a.xnu_source/d/relative for d in ('bsd','osfmk','iokit','libkern') if (a.xnu_source/d/relative).is_file()]
            if len(matches)!=1:break
            original=matches[0].resolve()
            if not original.is_relative_to(a.xnu_source.resolve()):raise ValueError('Header escaped XNU root')
            dest=extra/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(original,dest)
            added_headers[relative]={'source':str(original),'sha256':sha(original)}
            overlay['roots'].append({'type':'file','name':str(dest),'external-contents':str(dest)});write(vfs,overlay)
        if result.returncode:raise ValueError(name+' failed: '+result.stderr[-3000:])
        return inspect(name,target,command,Path(prior['output']) if filename is None else None)
    prior=next(v for v in nr.values() if v['source'].endswith('/CpuPerfSurvey.cpp'))
    for filename in plan['cpp_units']:
        name=Path(filename).stem
        defines=('ELDEN_BT_NATIVE_BUILD','ELDEN_BLUETOOTH_HCI_BUILD')+(('ELDEN_BT_RUNTIME_BUILD',) if a.profile in ('v99-bt-runtime','v101-bt-clock','v102-bt-wake') else ())
        audit[name]=compile_cpp(name,prior,nc,Path(inputs['native'])/'build',filename,defines=defines)
    core_replacements={}
    if a.profile in ('v99-bt-runtime','v101-bt-clock','v102-bt-wake'):
        from SourcePatch import patch
        for filename in ('kern_exec.c','ubc_subr.c'):
            selected=[(i,j) for i,j in enumerate(cc['jobs']) if j['file'].endswith('/'+filename)]
            if len(selected)!=1:raise ValueError('Core job is ambiguous')
            index,job=selected[0];body=Path(cc['files'][job['file']]['path']).read_bytes()
            derived=source/filename;derived.write_bytes(patch(filename,body))
            name=Path(filename).stem
            audit[name]=compile_cpp(name,cr[str(index)],cc,Path(inputs['core'])/'build',source_override=derived,scalar=True)
            expected=exports(Path(cr[str(index)]['output']))|({'_elden_bt_runtime_ready'} if filename=='kern_exec.c' else set())
            if exports(obj/(name+'.o'))!=expected:raise ValueError('Core exports changed unexpectedly')
            core_replacements[index]=obj/(name+'.o')
    parent=read(base/'native/build/results.json');pcap=read(base/'native/capture.json')
    audit['ShengUfs']=compile_cpp('ShengUfs',parent['0'],pcap,base/'native/build',overrides={'EldenSdamRecovery.inc':PROFILE/'EldenSdamRecovery.inc'})
    if sha(ROOT/plan['display_glue_path'])!=plan['display_glue_sha256']:raise ValueError('Display glue changed')
    audit['EldenDisplayGlue']=compile_cpp('EldenDisplayGlue',parent['2'],pcap,base/'native/build',defines=('ELDEN_WCN_POWER_BUILD','ELDEN_BLUETOOTH_BUILD'))
    if exports(obj/'ShengUfs.o')!=exports(Path(parent['0']['output'])) or exports(obj/'EldenDisplayGlue.o')!=exports(Path(parent['2']['output'])):
        raise ValueError('Existing runtime exports changed')
    for name,symbol in [('ShengUfs','_elden_wcn_request_stop'),('EldenDisplayGlue','_elden_wcn_power_start'),('NativeHost','_elden_wcn_stop_requested')]:
        if symbol not in global_symbols(obj/(name+'.o')):raise ValueError('Required WCN linkage absent: '+name)
    write(out/'object-audit.json',audit);write(out/'extra-headers.json',added_headers)
    archives={}
    for name,layout in bp['archives'].items():
        folder=out/'archives'/name;folder.mkdir(parents=True)
        receipt=read(base/'archives'/name/'result.json');original=base/'archives'/name/layout['archive']
        if sha(original)!=receipt['sha256']:raise ValueError('v94 archive changed')
        members=list(receipt['members'])
        for member in members:
            replacement=obj/member if member in ('ShengUfs.o','EldenDisplayGlue.o') else base/'archives'/name/member
            shutil.copyfile(replacement,folder/member)
        archives[name]=folder/layout['archive']
        subprocess.run(['/usr/bin/ar','rcs',str(archives[name]),*members],cwd=folder,env=dict(os.environ,ZERO_AR_DATE='1'),check=True)
    btarchive=out/'libEldenBluetooth.a';btmembers=[str(obj/(Path(n).stem+'.o')) for n in plan['c_units']+plan['cpp_units']]
    subprocess.run(['/usr/bin/ar','rcs',str(btarchive),*btmembers],env=dict(os.environ,ZERO_AR_DATE='1'),check=True)
    for value in ip['link_command']:
        for name in re.findall(r'@ARCHIVE:([^@]+)@',value):
            if name not in archives:
                record=read(Path(inputs['native'])/'archives'/name/'result.json');file=Path(inputs['native'])/'archives'/name/record['archive']
                if sha(file)!=record['sha256']:raise ValueError('Native archive changed')
                archives[name]=file
    video=read(base/'core/build/results.json')['0'];filelist=out/'link.filelist'
    if sha(Path(video['output']))!=video['sha256']:raise ValueError('Video object changed')
    filelist.write_text(''.join(str(core_replacements.get(i,Path(video['output']) if i==237 else Path(cr[str(i)]['output'])))+'\n' for i in ip['filelist_jobs']))
    target=out/'native.macho'
    def expand(v):
        v=v.replace('@CLANGXX@',clang).replace('@FILELIST@',str(filelist)).replace('@OUTPUT@',str(target)).replace('@RUNTIME@',str(image/'runtime'))
        v=re.sub(r'@OBJECT:([0-9]+)@',lambda m:str(core_replacements.get(int(m[1]),Path(video['output']) if int(m[1])==237 else Path(cr[m[1]]['output']))),v)
        v=re.sub(r'@ARCHIVE:([^@]+)@',lambda m:str(archives[m[1]]),v)
        v=re.sub(r'@LINK_INPUT:([^@]+)@',lambda m:str(image/m[1]),v)
        if '@' in v:raise ValueError('Unresolved link input')
        return v
    command=[expand(v) for v in ip['link_command']]+['-Wl,-force_load,'+str(btarchive)]
    env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    write(out/'link-command.json',command)
    with (logs/'link.log').open('x') as log:subprocess.run(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    expected=exports(base/'native.macho')
    for path in map(Path,btmembers):expected|=exports(path)
    if a.profile in ('v98-bt-bootstrap','v99-bt-runtime','v101-bt-clock','v102-bt-wake'):
        oid='_sysctl__kern_elden_bt_bootstrap'
        if not global_symbols(obj/'BootstrapStatus.o')[oid][0]&0x10:raise ValueError('Expected private sysctl OID')
        expected.remove(oid) # ld localizes this standard private-external OID.
    if a.profile in ('v99-bt-runtime','v101-bt-clock','v102-bt-wake'):expected.add('_elden_bt_runtime_ready')
    if exports(target)!=expected:raise ValueError('Linked exports differ from exact WCN addition')
    pre=ip['prelink'];kit=out/'prelink-tool';kit.mkdir()
    raw=subprocess.check_output(['git','-C',str(a.prelink_repository),'archive',pre['commit']])
    with tarfile.open(fileobj=io.BytesIO(raw)) as tar:tar.extractall(kit,filter='data')
    kernel=out/'kernel.macho';command=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(target),'--prelinked',str(a.original_kernelcache),'--slide='+pre['slide'],'--output',str(kernel)]
    for name in pre['roots']:command+=['--root',name]
    for v in pre['early_security_initializers']:command+=['--early-security-init',v]
    with (logs/'prelink.log').open('x') as log:subprocess.run(command,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
    verify_original_executables(kernel,read(kernel.with_suffix('.json')),pre['original_executable_sha256'])
    result={'profile':a.profile,'profile_sha256':sha(PROFILE/'plan.json'),'native_sha256':sha(target),'kernel_sha256':sha(kernel),
            'new_objects':len(btmembers),'recompiled_existing_objects':2,'original_apple_executables_hashed':30,'new_object_fp_simd':False,
            'radio_enabled':False,'boot_packaged':False,'hardware_tested':False,'device_operations':False,'sources':records}
    if core_replacements:result['recompiled_core_objects']=len(core_replacements);result['runtime_input']=runtime
    write(out/'result.json',result);print(json.dumps(result,indent=2))

if __name__=='__main__':main()
