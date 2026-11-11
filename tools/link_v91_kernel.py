#!/usr/bin/env python3
"""Link rebuilt v91 sources, verify the native image, and prelink original inputs."""
from pathlib import Path
import argparse,hashlib,io,json,os,plistlib,re,shutil,struct,subprocess,sys,tarfile
from replay_xnu_objects import global_symbols
ROOT=Path(__file__).resolve().parents[1];PROFILE=ROOT/'profiles/v91/image-build'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def native_identity(path):
    data=path.read_bytes();at=32;sections={};commands=[]
    if struct.unpack_from('<3I',data)!=(0xfeedfacf,0x100000c,0):raise ValueError('Expected ordinary ARM64 native kernel')
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        kind,length=struct.unpack_from('<II',data,at);skip=kind in (2,0x1b)
        if kind==25:
            segment=data[at+8:at+24].split(b'\0')[0].decode();skip=segment=='__LINKEDIT'
            for i in range(struct.unpack_from('<I',data,at+64)[0]):
                p=at+72+i*80;name=data[p:p+16].split(b'\0')[0].decode();seg=data[p+16:p+32].split(b'\0')[0].decode();address,size,offset=struct.unpack_from('<QQI',data,p+32);flags=struct.unpack_from('<I',data,p+64)[0]
                if seg=='__DWARF':continue
                body=b'' if flags&255 in (1,12,18) else data[offset:offset+size]
                sections[seg+'/'+name]={'address':address,'bytes':size,'flags':flags,'sha256':hashlib.sha256(body).hexdigest()}
        if not skip:commands.append({'command':kind,'sha256':hashlib.sha256(data[at:at+length]).hexdigest()})
        at+=length
    symbols=hashlib.sha256(json.dumps(global_symbols(path),sort_keys=True,separators=(',',':')).encode()).hexdigest()
    return sections,commands,symbols
def verify_original_executables(path,report,expected):
    data=path.read_bytes();at=32;segments=[]
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        kind,length=struct.unpack_from('<II',data,at)
        if kind==25:
            address,size,offset,filesize=struct.unpack_from('<4Q',data,at+24)
            segments.append((address,filesize,offset))
        at+=length
    actual={}
    for extent in report['extents']:
        if extent['segment']!='__TEXT_EXEC':continue
        address=extent['va'];size=extent['size'];matches=[s for s in segments if s[0]<=address and address+size<=s[0]+s[1]]
        if len(matches)!=1:raise RuntimeError('Original executable extent is not uniquely file-backed')
        start,_,offset=matches[0];body=data[offset+address-start:offset+address-start+size]
        if extent['module'] in actual:raise RuntimeError('Unexpected split original executable extent')
        actual[extent['module']]=hashlib.sha256(body).hexdigest()
    if actual!=expected:raise RuntimeError('Actual original Apple executable bytes differ')
    return len(actual)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--xnu-objects',type=Path,required=True,help='build/ from build_v91_xnu_objects.py')
    p.add_argument('--native-archives',type=Path,required=True,help='archives/ from build_v91_native_archives.py')
    p.add_argument('--prelink-repository',type=Path,required=True);p.add_argument('--original-kernelcache',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();plan=json.loads((PROFILE/'plan.json').read_text());out=a.output.resolve()
    if out.exists():raise RuntimeError('Output must be new')
    core=a.xnu_objects.resolve(strict=True);aroot=a.native_archives.resolve(strict=True)
    core_capture=json.loads((core.parent/'capture.json').read_text());native_capture=json.loads((aroot.parent/'capture.json').read_text())
    if core_capture['portable_profile_sha256']!=sha(ROOT/'profiles/v91/object-build/plan.json') or native_capture['profile_sha256']!=sha(ROOT/'profiles/v91/native-build/plan.json'):raise RuntimeError('Build outputs belong to a different source profile')
    objects=json.loads((core/'results.json').read_text());object_paths={}
    for key,item in objects.items():
        file=Path(item['output']).resolve(strict=True)
        if not item.get('accepted') or not file.is_relative_to(core/'objects') or sha(file)!=item['sha256']:raise RuntimeError('Unqualified XNU object')
        object_paths[key]=file
    if len(object_paths)!=1010:raise RuntimeError('Incomplete XNU object set')
    native_plan=json.loads((ROOT/'profiles/v91/native-build/plan.json').read_text());archives={}
    for name,layout in native_plan['archives'].items():
        folder=aroot/name;record=json.loads((folder/'result.json').read_text());file=folder/layout['archive']
        if not record.get('loadable_object_sections_equal') or not record.get('global_and_absolute_object_bindings_equal') or not record.get('public_symbol_sets_equal') or sha(file)!=record['sha256']:raise RuntimeError('Unqualified native archive: '+name)
        archives[name]=file
    clang=subprocess.check_output(['xcrun','--sdk','iphoneos','--find','clang++'],text=True).strip();resource=Path(subprocess.check_output([clang,'-print-resource-dir'],text=True).strip())
    compiler_runtime=resource/plan['compiler_runtime']['resource_path']
    if sha(compiler_runtime)!=plan['compiler_runtime']['sha256']:raise RuntimeError('Compiler runtime differs from v91')
    original=a.original_kernelcache.resolve(strict=True)
    if sha(original)!=plan['prelink']['original_kernelcache_sha256']:raise RuntimeError('Original 21H461 kernelcache identity mismatch')
    out.mkdir(parents=True);inputs={}
    for name,item in plan['link_inputs'].items():
        source=PROFILE/item['path'];data=source.read_bytes()
        if item['encoding']=='bplist-preserve-order':data=plistlib.dumps(json.loads(data),fmt=plistlib.FMT_BINARY,sort_keys=False)
        elif item['encoding']!='raw':raise RuntimeError('Unknown link-input encoding')
        if hashlib.sha256(data).hexdigest()!=item['sha256']:raise RuntimeError('Link metadata identity mismatch')
        dest=out/name;dest.write_bytes(data);inputs[name]=dest
    runtime=out/'runtime';runtime.mkdir();shutil.copyfile(archives['firehose'],runtime/'libfirehose_kernel.a')
    filelist=out/'link.filelist';filelist.write_text(''.join(str(object_paths[str(i)])+'\n' for i in plan['filelist_jobs']))
    native=out/'native.macho'
    def expand(value):
        value=value.replace('@CLANGXX@',clang).replace('@FILELIST@',str(filelist)).replace('@OUTPUT@',str(native)).replace('@RUNTIME@',str(runtime))
        value=re.sub(r'@OBJECT:([0-9]+)@',lambda m:str(object_paths[m[1]]),value)
        value=re.sub(r'@ARCHIVE:([^@]+)@',lambda m:str(archives[m[1]]),value)
        value=re.sub(r'@LINK_INPUT:([^@]+)@',lambda m:str(inputs[m[1]]),value)
        if '@' in value:raise RuntimeError('Unresolved linker input token')
        return value
    command=[expand(v) for v in plan['link_command']];env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    with (out/'link.log').open('x') as log:subprocess.run(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    sections,commands,symbols=native_identity(native)
    if sections!=plan['expected_native_sections'] or commands!=plan['expected_immutable_load_commands'] or symbols!=plan['expected_global_symbol_hash']:raise RuntimeError('Native loadable image does not match v91')
    pre=plan['prelink'];kit=out/'prelink-tool';kit.mkdir()
    archive=subprocess.check_output(['git','-C',str(a.prelink_repository),'archive','--format=tar',pre['commit']])
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:tar.extractall(kit,filter='data')
    kernel=out/'kernel.macho';cmd=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(native),'--prelinked',str(original),'--slide='+pre['slide'],'--output',str(kernel)]
    for name in pre['roots']:cmd+=['--root',name]
    for value in pre['early_security_initializers']:cmd+=['--early-security-init',value]
    with (out/'prelink.log').open('x') as log:subprocess.run(cmd,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
    report=json.loads(kernel.with_suffix('.json').read_text())
    if report['original_executable_sha256']!=pre['original_executable_sha256']:raise RuntimeError('Original Apple executable boundary changed')
    verify_original_executables(kernel,report,pre['original_executable_sha256'])
    result={'native_sha256':sha(native),'kernel_sha256':sha(kernel),'native_loadable_sections_equal':True,'native_immutable_commands_equal':True,'native_global_symbols_equal':True,'original_apple_executable_components':len(pre['original_executable_sha256']),'actual_original_executable_ranges_hashed':len(pre['original_executable_sha256']),'original_apple_executables_unchanged':True,'core_objects':len(object_paths),'project_archives':len(archives),'image_plan_sha256':sha(PROFILE/'plan.json'),'prelink_commit':pre['commit'],'boot_packaged':False,'device_operation':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
