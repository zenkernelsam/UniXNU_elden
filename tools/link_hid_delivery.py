"""Link the two qualified HID delivery objects into the exact v94 baseline."""
import argparse,hashlib,io,json,os,re,shutil,subprocess,sys,tarfile
from pathlib import Path
from build_fbwc_kernel import verified_objects
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
PROFILE=ROOT/'profiles/v100-hid-delivery/plan.json'
sys.path.insert(0,str(ROOT/'drivers/EldenHidDelivery'))
from SourcePatch import derive

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def write(p,value):p.write_text(json.dumps(value,indent=2)+'\n')
def exports(p):return {n for n,v in global_symbols(p).items() if v[0]&0x0e!=0}
def symbols_sha(p):return hashlib.sha256(json.dumps(global_symbols(p),sort_keys=True,separators=(',',':')).encode()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('v94-build','delivery-build','prelink-repository','original-kernelcache','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();base=a.v94_build.resolve();delivery=a.delivery_build.resolve();out=a.output.resolve();plan=read(PROFILE)
    if out.exists():raise ValueError('Output must be new')
    if sha(base/'native.macho')!=plan['baseline_native_sha256'] or sha(base/'kernel.macho')!=plan['baseline_kernel_sha256']:
        raise ValueError('Wrong v94 baseline')
    for path,expected in plan['owned'].items():
        if sha(ROOT/path)!=expected:raise ValueError('Changed delivery source: '+path)
    inputs=read(base/'inputs.json');bp=read(ROOT/'profiles/v94-fbwc-observation/plan.json')
    if sha(ROOT/'profiles/v94-fbwc-observation/plan.json')!=plan['baseline_profile_sha256']:raise ValueError('Changed v94 profile')
    cc,cr=verified_objects(Path(inputs['core']),bp['parent_core_profile'])
    nc,nr=verified_objects(Path(inputs['native']),bp['parent_native_profile'])
    ip=read(ROOT/'profiles/v91/image-build/plan.json');image=Path(inputs['image'])
    if sha(ROOT/'profiles/v91/image-build/plan.json')!=bp['parent_image_profile']:raise ValueError('Image link profile changed')
    for name,item in ip['link_inputs'].items():
        if sha(image/name)!=item['sha256']:raise ValueError('Changed link metadata')
    if sha(a.original_kernelcache)!=ip['prelink']['original_kernelcache_sha256']:raise ValueError('Wrong original kernelcache')
    report=read(delivery/'result.json');replacement={}
    if report['parent_capture_sha256']!=sha(Path(inputs['native'])/'capture.json') or not report['versioned_callback_pair']:
        raise ValueError('Wrong component parent or callback qualification')
    for name,expected in plan['objects'].items():
        item=report['objects'][name];obj=delivery/'objects'/(name+'.o');source=delivery/'source'/name
        if sha(source)!=expected['source_sha256'] or source.read_bytes()!=derive(name) or sha(obj)!=item['object_sha256']:
            raise ValueError('Component identity differs')
        if sections(obj)!=expected['sections'] or symbols_sha(obj)!=expected['symbols_sha256']:
            raise ValueError('Component code or bindings differ')
        if item['parent_job_index']!=expected['parent_job_index']:raise ValueError('Wrong archive job')
        replacement[expected['parent_job_index']]=obj
    video=read(base/'core/build/results.json')['0'];video_path=Path(video['output'])
    if sha(video_path)!=video['sha256']:raise ValueError('Changed framebuffer console object')
    expected_list=[str(video_path if i==237 else Path(cr[str(i)]['output'])) for i in ip['filelist_jobs']]
    if (base/'link.filelist').read_text().splitlines()!=expected_list:raise ValueError('Unexpected baseline core file list')
    out.mkdir(parents=True);(out/'objects').mkdir();(out/'source').mkdir()
    for name in plan['objects']:
        shutil.copyfile(delivery/'objects'/(name+'.o'),out/'objects'/(name+'.o'))
        shutil.copyfile(delivery/'source'/name,out/'source'/name)
    layout=bp['archives']['display'];oldfolder=base/'archives/display';oldarchive=oldfolder/layout['archive']
    if sha(oldarchive)!=read(oldfolder/'result.json')['sha256']:raise ValueError('Changed baseline display archive')
    def members(path):return [n for n in subprocess.check_output(['/usr/bin/ar','t',str(path)],text=True).splitlines() if not n.startswith('__.SYMDEF')]
    oldmembers=members(oldarchive);expectedmembers=[Path(nc['jobs'][i]['original_object']).name for i in layout['jobs']]+['MappingCheck.o']
    if oldmembers!=expectedmembers or len(set(oldmembers))!=len(oldmembers):raise ValueError('Unexpected archive membership')
    changed_names={Path(nc['jobs'][i]['original_object']).name:i for i in replacement}
    folder=out/'display';folder.mkdir();member_records={}
    for name in oldmembers:
        content=subprocess.check_output(['/usr/bin/ar','p',str(oldarchive),name]);oldhash=hashlib.sha256(content).hexdigest()
        original=(oldfolder/name).read_bytes()
        # Darwin ar includes newline padding in extracted Mach-O members.
        # Check the complete source plus exact8-byte alignment, not a prefix.
        if content!=original+b'\n'*(-len(original)%8):raise ValueError('Archive payload/padding differs from its member file')
        if name in changed_names:
            index=changed_names[name]
            if hashlib.sha256(original).hexdigest()!=nr[str(index)]['sha256']:raise ValueError('Original archive member is not the checked parent')
            content=replacement[index].read_bytes()
        (folder/name).write_bytes(content)
        member_records[name]={'before':oldhash,'after':hashlib.sha256(content).hexdigest()}
    archive=folder/layout['archive'];subprocess.run(['/usr/bin/ar','rcs',str(archive),*oldmembers],cwd=folder,env=dict(os.environ,ZERO_AR_DATE='1'),check=True)
    if members(archive)!=oldmembers:raise ValueError('New archive order changed')
    for name in oldmembers:
        source=(folder/name).read_bytes();stored=subprocess.check_output(['/usr/bin/ar','p',str(archive),name])
        if stored!=source+b'\n'*(-len(source)%8):raise ValueError('New archive payload/padding differs')
        member_records[name]['archived_after']=hashlib.sha256(stored).hexdigest()
    archives={'display':archive}
    for value in ip['link_command']:
        for name in re.findall(r'@ARCHIVE:([^@]+)@',value):
            if name in archives:continue
            if name=='ShengUfs':
                old=base/'archives'/name;record=read(old/'result.json');file=old/bp['archives'][name]['archive']
            else:
                old=Path(inputs['native'])/'archives'/name;record=read(old/'result.json');file=old/record['archive']
            if sha(file)!=record['sha256']:raise ValueError('Changed inherited archive')
            archives[name]=file
    if sha(image/'runtime/libfirehose_kernel.a')!=read(Path(inputs['native'])/'archives/firehose/result.json')['sha256']:
        raise ValueError('Changed implicit logging runtime')
    filelist=out/'link.filelist';shutil.copyfile(base/'link.filelist',filelist)
    native=out/'native.macho';clang=subprocess.check_output(['xcrun','--sdk','iphoneos','--find','clang++'],text=True).strip()
    def expand(value):
        value=value.replace('@CLANGXX@',clang).replace('@FILELIST@',str(filelist)).replace('@OUTPUT@',str(native)).replace('@RUNTIME@',str(image/'runtime'))
        value=re.sub(r'@OBJECT:([0-9]+)@',lambda m:str(video_path if int(m[1])==237 else Path(cr[m[1]]['output'])),value)
        value=re.sub(r'@ARCHIVE:([^@]+)@',lambda m:str(archives[m[1]]),value)
        value=re.sub(r'@LINK_INPUT:([^@]+)@',lambda m:str(image/m[1]),value)
        if '@' in value:raise ValueError('Unresolved link input')
        return value
    command=[expand(v) for v in ip['link_command']];write(out/'link-command.json',command)
    env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
    with (out/'link.log').open('x') as log:subprocess.run(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    if exports(native)!=(exports(base/'native.macho')-{'_native_touch_set_sink'})|{'_elden_touch_set_sink_v2'}:
        raise ValueError('Unexpected linked export difference')
    pre=ip['prelink'];kit=out/'prelink-tool';kit.mkdir()
    with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git','-C',str(a.prelink_repository),'archive',pre['commit']]))) as tar:tar.extractall(kit,filter='data')
    kernel=out/'kernel.macho';command=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(native),'--prelinked',str(a.original_kernelcache),'--slide='+pre['slide'],'--output',str(kernel)]
    for name in pre['roots']:command+=['--root',name]
    for name in pre['early_security_initializers']:command+=['--early-security-init',name]
    with (out/'prelink.log').open('x') as log:subprocess.run(command,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
    verify_original_executables(kernel,read(kernel.with_suffix('.json')),pre['original_executable_sha256'])
    result={'profile':'v100-hid-delivery','profile_sha256':sha(PROFILE),'kernel_sha256':sha(kernel),'native_sha256':sha(native),
        'baseline_kernel_sha256':plan['baseline_kernel_sha256'],'delivery_build_sha256':sha(delivery/'result.json'),
        'changed_archive_members':sorted(changed_names),'archive_members':member_records,'core_filelist_equal':True,
        'original_apple_executables_hashed':30,'hardware_tested':False,'boot_packaged':False,'device_operations':False}
    write(out/'result.json',result);print(json.dumps({k:result[k] for k in ('profile','kernel_sha256','native_sha256','changed_archive_members')}))
if __name__=='__main__':main()
