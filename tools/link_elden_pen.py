"""Link the pen service and replace the exact original touch worker only.
Preserve every other archive member, baseline core inputs and original Apple
execution region. No boot image or device operation.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
from replay_xnu_objects import global_symbols,sections
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def defs(p):return {k for k,v in global_symbols(p).items() if v[0]&0xe}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('base','base-native','pen-build','original-kernelcache','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    base=a.base.resolve();native=a.pen_build.resolve();prior=read(base/'result.json');report=read(native/'result.json')
    for name in ('native','kernel'):
        if sha(base/(name+'.macho'))!=prior[name+'_sha256']:raise ValueError('Baseline image changed')
    if not report['compiled'] or not report['touch_hook_compiled']:raise ValueError('Pen/touch components incomplete')
    for name,row in report['objects'].items():
        if sha(native/(Path(name).stem+'.o'))!=row['sha256']:raise ValueError('Pen object changed')
    pen=native/'libEldenPen.a'
    if sha(pen)!=report['archive_sha256']:raise ValueError('Pen archive changed')
    pen_members=subprocess.check_output(['ar','t',str(pen)],text=True).splitlines()
    for need in ('Owner.o','NativeBridge.o'):
        if pen_members.count(need)!=1:raise ValueError('Pen archive dropped bridge member: '+need)
    command=read(base/'link-command.json');args=[v for v in command if v.startswith('-Wl,-force_load,') and v.endswith('/libNativeLegacyDisplay.late.a')]
    if len(args)!=1:raise ValueError('Expected one display archive')
    original=Path(args[0].split(',',2)[2]);cap=read(a.base_native/'capture.json')
    jobs=[(i,j) for i,j in enumerate(cap['jobs']) if j['file'].endswith('/EldenNvt36536/Runtime.cpp')]
    if len(jobs)!=1:raise ValueError('Unique touch capture required')
    index,job=jobs[0];member=Path(job['original_object']).name
    if member!='EldenNvtRuntime.o':raise ValueError('Touch archive layout changed')
    oldrow=read(a.base_native/'build/results.json')[str(index)];old=Path(oldrow['output'])
    if sha(old)!=oldrow['sha256']:raise ValueError('Captured touch object changed')
    replacement=native/'EldenTouchRuntime.o'
    before=global_symbols(old);after=global_symbols(replacement)
    if defs(old)!=defs(replacement):raise ValueError('Touch exports changed')
    added=set(after)-set(before)
    if added!={'_elden_pen_native_start','_elden_pen_native_packet','_elden_pen_native_fault','_elden_pen_native_stop'}:
        raise ValueError('Unexpected new touch imports: '+str(added))
    if sections(old).get('__DATA/__mod_init_func')!=sections(replacement).get('__DATA/__mod_init_func'):raise ValueError('Touch initializers changed')
    names=[n for n in subprocess.check_output(['ar','t',str(original)],text=True).splitlines() if not n.startswith('__.SYMDEF')]
    if names.count(member)!=1 or len(set(names))!=len(names):raise ValueError('Archive has duplicate/absent members')
    folder=out/'display';folder.mkdir();members={}
    for name in names:
        if Path(name).name!=name:raise ValueError('Invalid member path')
        data=subprocess.check_output(['ar','p',str(original),name]);digest=hashlib.sha256(data).hexdigest()
        if name==member:
            expected=old.read_bytes()
            if data!=expected and data!=expected+b'\n'*(-len(expected)%8):raise ValueError('Baseline archive is not the source-built touch member')
            data=replacement.read_bytes()
        (folder/name).write_bytes(data);members[name]={'before':digest,'after':hashlib.sha256(data).hexdigest()}
    archive=folder/original.name
    subprocess.run(['ar','rcs',str(archive),*names],cwd=folder,env=dict(os.environ,ZERO_AR_DATE='1'),check=True)
    for name in names:
        data=(folder/name).read_bytes();stored=subprocess.check_output(['ar','p',str(archive),name])
        if stored!=data and stored!=data+b'\n'*(-len(data)%8):raise ValueError('Archive readback differs')
    inherited={}
    for arg in command:
        path=Path(arg.split(',')[-1])
        if path.is_file() and path.suffix in ('.o','.a'):inherited[str(path)]=sha(path)
        if path.is_file() and path.name.endswith('filelist'):
            for line in path.read_text().splitlines():inherited[line]=sha(Path(line))
    command[command.index(args[0])]='-Wl,-force_load,'+str(archive)
    command[command.index('-o')+1]=str(out/'native.macho');command+=['-Wl,-force_load,'+str(pen)]
    (out/'link-command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (out/'native-link.log').open('x') as f:subprocess.run(command,cwd=out,stdout=f,stderr=subprocess.STDOUT,check=True)
    for path,digest in inherited.items():
        if sha(Path(path))!=digest:raise ValueError('Baseline input changed during link')
    bridge_exports={n for n in defs(out/'native.macho') if n.startswith(('_elden_pen_bridge_','_elden_pen_owner_'))}
    expected_exports={'_elden_pen_owner_init','_elden_pen_owner_bind','_elden_pen_owner_packet','_elden_pen_owner_tick',
        '_elden_pen_owner_fault','_elden_pen_owner_pause','_elden_pen_owner_resume','_elden_pen_owner_stop',
        '_elden_pen_owner_release','_elden_pen_owner_retire','_elden_pen_owner_released','_elden_pen_owner_status',
        '_elden_pen_bridge_init','_elden_pen_bridge_bind','_elden_pen_bridge_start','_elden_pen_bridge_packet',
        '_elden_pen_bridge_tick','_elden_pen_bridge_fault','_elden_pen_bridge_pause','_elden_pen_bridge_resume',
        '_elden_pen_bridge_stop','_elden_pen_bridge_release','_elden_pen_bridge_retire','_elden_pen_bridge_status',
        '_elden_pen_bridge_started','_elden_pen_bridge_bound'}
    if bridge_exports!=expected_exports:raise ValueError('Native link changed/dropped bridge exports: '+str(sorted(bridge_exports^expected_exports)))
    image=read(base/'kernel.json')
    if sha(a.original_kernelcache)!=image['original_sha256']:raise ValueError('Original kernelcache differs')
    plan=read(ROOT/'profiles/v91/image-build/plan.json')['prelink']
    command=[sys.executable,str(base/'prelink-tool/emit_prelink.py'),'--kernel',str(out/'native.macho'),
             '--prelinked',str(a.original_kernelcache),'--slide='+str(image['slide']),'--output',str(out/'kernel.macho')]
    for name in plan['roots']:command+=['--root',name]
    for name in plan['early_security_initializers']:command+=['--early-security-init',name]
    with (out/'prelink.log').open('x') as f:subprocess.run(command,cwd=out,stdout=f,stderr=subprocess.STDOUT,check=True)
    final=read(out/'kernel.json');count=verify_original_executables(out/'kernel.macho',final,image['original_executable_sha256'])
    result={'passed':True,'native_sha256':sha(out/'native.macho'),'kernel_sha256':sha(out/'kernel.macho'),
            'original_regions_preserved':count,'display_members':members,'touch_new_imports':sorted(added),
            'new_exports':sorted(defs(out/'native.macho')-defs(base/'native.macho')),
            'pen_archive_members':pen_members,'bridge_exports':sorted(bridge_exports),
            'inherited_inputs':inherited,'pen_report_sha256':sha(native/'result.json'),
            'default_off':True,'boot_packaged':False,'hardware':False,'native_bound':False,'pencil_accepted':False,
            'legacy_emitter_flags':{k:final.get(k) for k in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: pen/touch full native link; original Apple execution regions preserved; no boot/hardware claim')
if __name__=='__main__':main()
