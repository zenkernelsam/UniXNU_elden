#!/usr/bin/env python3
"""Build the five reviewed framebuffer units against verified v91 build outputs.

All unchanged objects are reused by full hash. No historical workbench build
recipe is executed. Outputs are new and do not access the tablet.
"""
from pathlib import Path
import argparse,copy,hashlib,io,json,os,re,shutil,subprocess,sys,tarfile
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import native_identity,verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
PROFILE=ROOT/'profiles/v93-fbwc'
def source_file(name):
    parents={'v95-fbwc-pairs':'v94-fbwc-observation','v94-fbwc-observation':'v93-fbwc'}
    folder=PROFILE.name
    while True:
        local=ROOT/'profiles'/folder/'sources'/name
        if local.is_file():return local
        if folder not in parents:raise ValueError('Profile source missing: '+name)
        folder=parents[folder]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,d):p.write_text(json.dumps(d,indent=2)+'\n')

def console_vectors(assembly):
    functions={};name=''
    for line in assembly.splitlines():
        label=re.match(r'^[0-9a-f]+\s+<(.+)>:',line)
        if label:name=label[1];functions[name]=[]
        functions.setdefault(name,[]).append(line)
    vectors={}
    for name,lines in functions.items():
        ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$','\n'.join(lines),re.M)
        vectors[name]=[x for x in ops if re.search(r'\b[qvdshb]\d+(?:\b|\.)',x)]
    return functions,vectors

def check_console_early_return(current,prior):
    funcs,new=console_vectors(current);_,old=console_vectors(prior)
    for name in set(new)|set(old):
        a,b=new.get(name,[]),old.get(name,[])
        if name=='_initialize_screen':
            # The new scalar guard adds16 bytes of stack locals. Existing vector
            # copies after the guard keep the same operands except these slots.
            a=[x.replace('[sp, #0x70]','[sp, #0x60]') for x in a]
        elif name=='_vcattach':
            # The larger function is no longer inlined at this existing call.
            if a or not any('_initialize_screen' in x for x in funcs.get(name,[])):
                raise ValueError('Unexpected console wrapper code')
            continue
        if a!=b:raise ValueError('Unexpected console vector change: '+name)
    early=[]
    for line in funcs['_initialize_screen']:
        early.append(line)
        if re.search(r'\bret\s*$',line):break
    if not any('ret' in x for x in early) or not any('_PE_parse_boot_argn' in x for x in early):raise ValueError('Console guard missing')
    ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$','\n'.join(early),re.M)
    if any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',x) for x in ops):raise ValueError('Console early return uses SIMD')
    return {'early_return_scalar':True,'remaining_vectors_equal_except_stack_slots_and_removed_inlining':True}

def verified_objects(base,expected):
    cap=read(base/'capture.json');results=read(base/'build/results.json')
    actual=cap.get('profile_sha256',cap.get('portable_profile_sha256'))
    if actual!=expected or (base/'build/capture.sha256').read_text().strip()!=sha(base/'capture.json'):
        raise ValueError('Wrong v91 source capture')
    for item in results.values():
        p=Path(item['output']).resolve(strict=True)
        if not p.is_relative_to(base/'build/objects') or not item.get('accepted') or sha(p)!=item['sha256']:
            raise ValueError('Changed or unqualified v91 object')
    return cap,results

def external(cap,suffix):
    found=[v for k,v in cap['files'].items() if k.endswith('/'+suffix)]
    if not found or len({v['sha256'] for v in found})!=1:raise ValueError('Ambiguous external header: '+suffix)
    return Path(found[0]['path']).resolve(strict=True)

def main():
    global PROFILE
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--profile',choices=('v93-fbwc','v94-fbwc-observation','v95-fbwc-pairs'),default='v93-fbwc')
    for n in ('v91-core','v91-native','v91-image','xnu-repository','prelink-repository','original-kernelcache','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--resume',action='store_true',help='Resume only the same source profile before archive/link creation')
    a=p.parse_args();PROFILE=ROOT/'profiles'/a.profile;plan=read(PROFILE/'plan.json');out=a.output.resolve()
    if out.exists() and (not a.resume or (out/'archives').exists() or (out/'native.macho').exists()):raise ValueError('Output must be new, or the same pre-link build with --resume')
    core=a.v91_core.resolve(strict=True);native=a.v91_native.resolve(strict=True);image=a.v91_image.resolve(strict=True);llvm=a.llvm.resolve(strict=True)
    cc,cr=verified_objects(core,plan['parent_core_profile']);nc,nr=verified_objects(native,plan['parent_native_profile'])
    if len(cr)!=1010 or len(nr)!=608:raise ValueError('Incomplete baseline builds')
    ip=read(ROOT/'profiles/v91/image-build/plan.json')
    if sha(ROOT/'profiles/v91/image-build/plan.json')!=plan['parent_image_profile']:raise ValueError('Image profile changed')
    baseline_native=image/'native.macho';ns,commands,symbol_hash=native_identity(baseline_native)
    if ns!=ip['expected_native_sections'] or commands!=ip['expected_immutable_load_commands'] or symbol_hash!=ip['expected_global_symbol_hash']:
        raise ValueError('Baseline linked image is not qualified v91')
    for name,item in ip['link_inputs'].items():
        if sha(image/name)!=item['sha256']:raise ValueError('Link metadata changed: '+name)
    if sha(a.original_kernelcache)!=ip['prelink']['original_kernelcache_sha256']:raise ValueError('Original Apple kernelcache changed')
    for name,h in plan['sources'].items():
        if sha(source_file(name))!=h:raise ValueError('Framebuffer source changed: '+name)
    inputs={'profile_sha256':sha(PROFILE/'plan.json'),'core':str(core),'native':str(native),'image':str(image),'native_sha256':sha(baseline_native),'source_files':plan['sources']}
    if out.exists():
        if read(out/'inputs.json')!=inputs:raise ValueError('Resume inputs changed')
    else:out.mkdir(parents=True);write(out/'inputs.json',inputs)
    host=out/'host';host.mkdir(exist_ok=True)
    if not (host/'result.json').exists():
        subprocess.run(['xcrun','--sdk','macosx','clang','-std=c11','-O1','-fsanitize=address,undefined',str(source_file('TestWcPolicy.c')),'-o',str(host/'test-policy')],check=True)
        result=subprocess.run([str(host/'test-policy')],capture_output=True,text=True,check=True)
        write(host/'result.json',{'passed':True,'stdout':result.stdout,'stderr':result.stderr})
    elif not read(host/'result.json')['passed']:raise ValueError('Host policy test failed')
    if 'TestCopyPairs.c' in plan['sources'] and not (host/'copy-result.json').exists():
        virtual=nc['jobs'][587]['file'].rsplit('/',1)[0]+'/NativeLegacyRequest.h'
        item=nc['files'][virtual];original=Path(item['path'])
        if sha(original)!=item['sha256']:raise ValueError('Copy bounds header changed')
        shutil.copyfile(original,host/'NativeLegacyRequest.h')
        subprocess.run(['xcrun','--sdk','macosx','clang','-std=c11','-O1','-fsanitize=address,undefined','-I'+str(host),str(source_file('TestCopyPairs.c')),'-o',str(host/'test-copy')],check=True)
        trial=subprocess.run([str(host/'test-copy')],capture_output=True,text=True,check=True)
        write(host/'copy-result.json',{'passed':True,'stdout':trial.stdout,'stderr':trial.stderr,'bounds_header_sha256':item['sha256']})
    payload=external(cc,'EldenIconCpuPayload.h');nvt=external(nc,'EldenNvtFirmware.h')

    def replace_file(cap,virtual,name):
        f=source_file(name)
        cap['files'][virtual]={'path':str(f),'sha256':sha(f),'bytes':f.stat().st_size}

    def changed_job(job):
        job=copy.deepcopy(job)
        job.pop('expected_sections',None);job.pop('expected_symbols',None)
        return job

    # The compiler frontend uses the parent's closed VFS and exactly its flags.
    ccap=copy.deepcopy(cc);video=changed_job(cc['jobs'][237]);mapping=changed_job(cc['jobs'][113])
    old=mapping['file'];folder=nc['jobs'][589]['file'].rsplit('/',1)[0]
    mapping['file']=folder+'/MappingCheck.c'
    mapping['arguments']=[mapping['file'] if x==old else x for x in mapping['arguments']]+['-mgeneral-regs-only']
    mapping['original_object']=folder+'/MappingCheck.o'
    ccap['jobs']=[video,mapping]
    replace_file(ccap,video['file'],'video_console.c');replace_file(ccap,mapping['file'],'MappingCheck.c');replace_file(ccap,folder+'/WcPolicy.h','WcPolicy.h')
    ncap=copy.deepcopy(nc);ncap['jobs']=[changed_job(nc['jobs'][i]) for i in (582,587,589)]
    replace_file(ncap,ncap['jobs'][0]['file'].rsplit('/',1)[0]+'/EldenSdamRecovery.inc','EldenSdamRecovery.inc')
    for job,name in zip(ncap['jobs'][1:],('NativeLegacyDisplay.cpp','EldenDisplayGlue.cpp')):
        replace_file(ncap,job['file'],name);replace_file(ncap,job['file'].rsplit('/',1)[0]+'/WcPolicy.h','WcPolicy.h')
    if 'CopyPairs.h' in plan['sources']:
        replace_file(ncap,ncap['jobs'][1]['file'].rsplit('/',1)[0]+'/CopyPairs.h','CopyPairs.h')
    compiled=[]
    for name,cap in [('core',ccap),('native',ncap)]:
        target=out/name;target.mkdir(exist_ok=True)
        if (target/'capture.json').exists():
            if read(target/'capture.json')!=cap:raise ValueError('Source capture changed')
        else:write(target/'capture.json',cap)
        # Original source paths share this fixed virtual root, independently of actual checkout locations.
        subprocess.run([sys.executable,str(ROOT/'tools/replay_xnu_objects.py'),'--capture',str(target/'capture.json'),'--historical-root',plan['virtual_root'],'--output',str(target/'build'),'--payload-header',str(payload),'--nvt-header',str(nvt),'--xnu-repository',str(a.xnu_repository),'--llvm-objcopy',str(llvm/'llvm-objcopy'),'--jobs','3']+(['--resume'] if (target/'build').exists() else []),check=True)
        compiled.append(read(target/'build/results.json'))
    cnew,nnew=compiled
    exports=lambda file:{n for n,v in global_symbols(file).items() if v[0]&0x0e!=0}
    artifacts={'video':Path(cnew['0']['output']),'mapping':Path(cnew['1']['output']),'ufs':Path(nnew['0']['output']),'display':Path(nnew['1']['output']),'glue':Path(nnew['2']['output'])}
    declared={'video':(Path(cr['237']['output']),{'_elden_fb_console_state'}),'ufs':(Path(nr['582']['output']),{'_elden_smp_framebuffer_ready'}),'display':(Path(nr['587']['output']),set()),'glue':(Path(nr['589']['output']),{'_native_legacy_fb_cache_mode'})}
    for name,(prior,added) in declared.items():
        if exports(artifacts[name])!=exports(prior)|added:raise ValueError('Unexpected export change: '+name)
    if exports(artifacts['mapping'])!={'_elden_fb_check_mapping'}:raise ValueError('Unexpected mapping-check exports')
    audit={}
    for name,file in artifacts.items():
        asm=subprocess.check_output([str(llvm/'llvm-objdump'),'--disassemble','--reloc',str(file)],text=True)
        (out/(name+'.asm')).write_text(asm)
        ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',asm,re.M)
        bad=[x for x in ops if re.search(r'\b[qvdshb]\d+(?:\b|\.)',x)]
        if not ops:raise ValueError('Missing disassembly: '+name)
        if name=='video':
            parent_asm=subprocess.check_output([str(llvm/'llvm-objdump'),'--disassemble','--reloc',cr['237']['output']],text=True)
            write(out/'console-guard-audit.json',check_console_early_return(asm,parent_asm))
        elif bad:raise ValueError('Unexpected FP/SIMD code: '+name+str(bad[:3]))
        if name=='display' and 'CopyPairs.h' in plan['sources']:
            funcs,_=console_vectors(asm)
            found=[v for k,v in funcs.items() if 'elden_fb_copy_pairs' in k]
            if len(found)!=1 or not re.search(r'\bstp\s+x\d+, x\d+, \[x\d+', '\n'.join(found[0])):
                raise ValueError('Paired data-store implementation missing')
            write(out/'pair-codegen.json',{'paired_data_stores_present':True,'scalar_function':True,'function_lines':found[0]})
        if name=='mapping':
            for mnemonic in ('s1e1r','s1e1w','par_el1','mair_el1'):
                if mnemonic not in asm.lower():raise ValueError('Missing translation instruction: '+mnemonic)
        audit[name]={'sha256':sha(file),'instructions':len(ops),'fp_simd':bool(bad),'baseline_console_simd_preserved':name=='video','sections':sections(file)}
    write(out/'object-audit.json',audit)
    archives={}
    for name,layout in plan['archives'].items():
        folder=out/'archives'/name;folder.mkdir(parents=True)
        members=[]
        for i in layout['jobs']:
            source={582:artifacts['ufs'],587:artifacts['display'],589:artifacts['glue']}.get(i,Path(nr[str(i)]['output']))
            member=Path(nc['jobs'][i]['original_object']).name
            shutil.copyfile(source,folder/member);members.append(member)
        if name=='display':shutil.copyfile(artifacts['mapping'],folder/'MappingCheck.o');members.append('MappingCheck.o')
        archive=folder/layout['archive'];subprocess.run(['/usr/bin/ar','rcs',str(archive),*members],cwd=folder,env=dict(os.environ,ZERO_AR_DATE='1'),check=True);archives[name]=archive
        actual=[x for x in subprocess.check_output(['/usr/bin/ar','t',str(archive)],text=True).splitlines() if not x.startswith('__.SYMDEF')]
        if actual!=members:raise ValueError('Archive member ordering changed')
        write(folder/'result.json',{'sha256':sha(archive),'members':members,'changed_jobs':[i for i in layout['jobs'] if i in (582,587,589)],'added_mapping_check':name=='display'})
    # Unmodified archives remain the already verified source-built ones.
    for value in ip['link_command']:
        for name in re.findall(r'@ARCHIVE:([^@]+)@',value):
            if name not in archives:
                rec=read(native/'archives'/name/'result.json');file=native/'archives'/name/rec['archive']
                if sha(file)!=rec['sha256']:raise ValueError('Unchanged archive modified: '+name)
                archives[name]=file
    if sha(image/'runtime/libfirehose_kernel.a')!=read(native/'archives/firehose/result.json')['sha256']:
        raise ValueError('Implicit logging runtime archive changed')
    filelist=out/'link.filelist';filelist.write_text(''.join(str(artifacts['video'] if i==237 else Path(cr[str(i)]['output']))+'\n' for i in ip['filelist_jobs']))
    native_image=out/'native.macho';clang=subprocess.check_output(['xcrun','--sdk','iphoneos','--find','clang++'],text=True).strip()
    def expand(value):
        value=value.replace('@CLANGXX@',clang).replace('@FILELIST@',str(filelist)).replace('@OUTPUT@',str(native_image)).replace('@RUNTIME@',str(image/'runtime'))
        value=re.sub(r'@OBJECT:([0-9]+)@',lambda m:str(artifacts['video'] if int(m[1])==237 else Path(cr[m[1]]['output'])),value)
        value=re.sub(r'@ARCHIVE:([^@]+)@',lambda m:str(archives[m[1]]),value)
        value=re.sub(r'@LINK_INPUT:([^@]+)@',lambda m:str(image/m[1]),value)
        if '@' in value:raise ValueError('Unresolved link input')
        return value
    command=[expand(x) for x in ip['link_command']];env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    write(out/'link-command.json',command)
    with (out/'link.log').open('x') as log:subprocess.run(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    if exports(native_image)!=exports(baseline_native)|set(plan['added_exports']):raise ValueError('Linked image exports changed outside framebuffer scope')
    kit=out/'prelink-tool';kit.mkdir();pre=ip['prelink']
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(a.prelink_repository),'archive',pre['commit']]))) as tar:tar.extractall(kit,filter='data')
    kernel=out/'kernel.macho';cmd=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(native_image),'--prelinked',str(a.original_kernelcache),'--slide='+pre['slide'],'--output',str(kernel)]
    for x in pre['roots']:cmd+=['--root',x]
    for x in pre['early_security_initializers']:cmd+=['--early-security-init',x]
    with (out/'prelink.log').open('x') as log:subprocess.run(cmd,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
    report=read(kernel.with_suffix('.json'));verify_original_executables(kernel,report,pre['original_executable_sha256'])
    result={'profile':a.profile,'kernel_sha256':sha(kernel),'native_sha256':sha(native_image),'profile_sha256':sha(PROFILE/'plan.json'),'recompiled_objects':5,'core_objects_reused':1009,'original_apple_executables_hashed':30,'added_exports':plan['added_exports'],'hardware_tested':False,'device_operation':False}
    write(out/'result.json',result);print(json.dumps(result,indent=2))

if __name__=='__main__':main()
