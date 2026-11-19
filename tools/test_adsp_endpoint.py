"""Run actual concrete sensor endpoint methods and native ARM64 build."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('source','base-native','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--channel-source',type=Path,required=True)
    a=p.parse_args()
    for n in ('source','base_native','llvm','channel_source'):setattr(a,n,getattr(a,n).resolve(strict=True))
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);record=json.loads((a.source/'source.json').read_text())
    for n,d in record['sources'].items():
        if sha(a.source/n)!=d:raise ValueError('Endpoint source changed')
    def run(cmd,label,cwd=None):
        r=subprocess.run(list(map(str,cmd)),cwd=cwd,capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; retained original log')
        return r.stdout.decode()
    run(['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source,DRIVER/'TestAdspEndpoint.cpp','-o',out/'test'],'host-build')
    host=run([out/'test'],'host-run')
    channel=json.loads((a.channel_source/'source.json').read_text())
    for n,d in channel['sources'].items():
        if sha(a.channel_source/n)!=d:raise ValueError('Actual original channel source changed')
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source,'-I',a.channel_source]
    run(['xcrun','clang','-std=c11',*flags,'-c',a.channel_source/'QcomGlink.c','-o',out/'Glink-host.o'],'stack-glink')
    run(['xcrun','clang++','-std=c++17',*flags,'-DELDEN_TEST_SENSOR_ENDPOINT=1',DRIVER/'TestAdspChannels.cpp',out/'Glink-host.o','-o',out/'stack-test'],'stack-build')
    stack=run([out/'stack-test'],'stack-run')
    cap=json.loads((a.base_native/'capture.json').read_text());job=next(j for j in cap['jobs'] if j['file'].endswith('/NativeHidInput.cpp'))
    old=job['file'].split('/research/',1)[0];project=a.base_native/'build/project';remap=lambda v:re.sub(re.escape(old)+r'/(research/)?',lambda m:str(project)+('/records/' if m[1] else '/'),v)
    for n,row in cap['files'].items():
        if sha(Path(remap(n)))!=row['sha256']:raise ValueError('Original XNU headers changed')
    flags=[];i=0
    while i<len(job['arguments']):
        v=job['arguments'][i]
        if v in ('-o','-MF','-MJ','-MT','-MQ','-serialize-diagnostics'):i+=2;continue
        if v==job['file'] or v in ('-MD','-MP','-MMD','-Wno-unnecessary-virtual-specifier'):i+=1;continue
        flags.append(remap(v));i+=1
    obj=out/'AdspEndpoint.o';command=[*flags,'-I',a.source,'-Werror','-mgeneral-regs-only',a.source/'AdspEndpoint.cpp','-o',obj]
    run(command,'native-build',remap(job['directory']));dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'native-asm')
    ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Unexpected SIMD/FP')
    if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic endpoint startup')
    result={'passed':True,'host_output':host,'stack_output':stack,'objects':{obj.name:{'sha256':sha(obj),'imports':[n for n,v in global_symbols(obj).items() if not v[0]&0xe]}},
        'source_record_sha256':sha(a.source/'source.json'),'actual_endpoint_methods':True,'OS_channel_memory_owner_peers_modeled':True,
        'hardware':False,'actual_shared_retirement_provider':False,'full_link':False,'native_command':list(map(str,command))}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(host+stack)
if __name__=='__main__':main()
