#!/usr/bin/env python3
"""Build the current Elden kernel/boot from a clone and declared original inputs.

macOS/Xcode is the native build host. This tool does not open a tablet. A state
journal only reuses outputs produced by completed steps in this exact run and
with the same source/configuration fingerprints; it never consumes v99 builds.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def main():
    if sys.flags.optimize:raise ValueError('Assertions must remain enabled')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inputs',type=Path,required=True,help='JSON with original inputs/toolchains; paths relative to JSON')
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--target',choices=('kernel','boot'),default='boot')
    p.add_argument('--variant',choices=('bluetooth','gui'),default='bluetooth',help='Frozen v121 Bluetooth diagnostics or v109 8GiB GUI')
    p.add_argument('--jobs',type=int,choices=range(1,9),default=4)
    p.add_argument('--resume',action='store_true',help='Resume completed stages of this exact source/input run')
    a=p.parse_args()
    if sys.platform!='darwin':raise SystemExit('Native build requires macOS with Xcode/iPhoneOS SDK. Windows/WSL can inspect sources and flash separately.')
    config=a.inputs.resolve();data=read(config);out=a.output.resolve()
    def input_path(name):
        value=Path(data[name]);return (value if value.is_absolute() else config.parent/value).resolve(strict=True)
    needed=['llvm','original_agent','nvt_firmware','original_kernelcache']
    if a.variant=='bluetooth':needed+=['wcn_firmware']
    if a.target=='boot':needed+=['os_image','crypto','mu_repository','unixnu_repository','stock_boot','parent_vbmeta','avbtool','signing_key']
    missing=[name for name in needed if name not in data]
    if missing:raise SystemExit('Missing declared inputs: '+', '.join(missing))
    inputs={name:input_path(name) for name in needed}
    from clean_input_check import check
    check(inputs,a.variant,a.target)
    # Source-only journal: untracked new source files are intentionally included.
    sources={}
    for folder in ('tools','profiles','drivers','scripts','patches'):
        for f in (ROOT/folder).rglob('*'):
            if f.is_file() and f.suffix in ('.py','.json','.c','.cpp','.h','.inc','.s','.S','.patch','.inf','.dsc','.fdf','.toml') and '__pycache__' not in f.parts:
                sources[f.relative_to(ROOT).as_posix()]=sha(f)
    identity={'sources':sources,'inputs':{n:str(v) for n,v in inputs.items()},'config_sha256':sha(config),'target':a.target,'variant':a.variant}
    stamp=hashlib.sha256(json.dumps(identity,sort_keys=True).encode()).hexdigest();journal=out/'build-state.json'
    if out.exists():
        if not a.resume or not journal.exists():raise ValueError('Use a new output, or explicitly resume this run')
        state=read(journal)
        if state['identity_sha256']!=stamp:raise ValueError('Source or declared input configuration changed; use a new output')
    else:
        out.mkdir(parents=True);state={'identity_sha256':stamp,'completed':{},'device_operations':False};(out/'build-inputs.json').write_text(json.dumps(identity,indent=2)+'\n')
    def save():journal.write_text(json.dumps(state,indent=2)+'\n')
    save();logs=out/'logs';logs.mkdir(exist_ok=True)
    def stage(name,command,proof):
        proof=out/proof
        if name in state['completed']:
            if not proof.is_file() or sha(proof)!=state['completed'][name]['proof_sha256']:raise ValueError('Completed stage proof changed: '+name)
            return
        state['active']=name;save();print('Building '+name,flush=True)
        with (logs/(name+'.log')).open('ab') as f:r=subprocess.run(list(map(str,command)),cwd=ROOT,stdout=f,stderr=subprocess.STDOUT)
        if r.returncode:raise SystemExit(name+' failed; inspect '+str(logs/(name+'.log')))
        if not proof.is_file():raise ValueError('Missing stage proof: '+name)
        state['completed'][name]={'proof':str(proof.relative_to(out)),'proof_sha256':sha(proof)};state.pop('active',None);save()
    def script(name,*args):return [sys.executable,'-B',ROOT/'tools'/name,*args]
    deps=out/'dependencies';deps.mkdir(exist_ok=True)
    locks=read(ROOT/'profiles/v91/native-build/plan.json')['dependencies']
    locks.update(xnu={'url':'https://github.com/apple-oss-distributions/xnu.git','commit':'d8b80295118ef25ac3a784134bcf95cd8e88109f'},
                 XnuPrelink={'url':'https://github.com/UniXNU/XnuPrelink.git','commit':read(ROOT/'profiles/v91/image-build/plan.json')['prelink']['commit']})
    for name,row in locks.items():
        repo=deps/name
        if not repo.exists():
            subprocess.run(['git','init','--quiet',repo],check=True);subprocess.run(['git','-C',repo,'remote','add','origin',row['url']],check=True)
        result=subprocess.run(['git','-C',repo,'cat-file','-e',row['commit']+'^{commit}'],capture_output=True)
        if result.returncode:subprocess.run(['git','-C',repo,'-c','http.version=HTTP/1.1','fetch','--depth=1','origin',row['commit']],check=True)
        # Object-store access only; no checkout/reset or source-host directory reuse.
    stage('reconstruct',script('reconstruct_v91.py','--xnu-repository',deps/'xnu','--output',out/'source'),'source/result.json')
    stage('icon',script('build_icon_v91.py','--output',out/'icon'),'icon/result.json')
    stage('agent',script('prepare_icon_agent_v91.py','--original-agent',inputs['original_agent'],'--library',out/'icon/EldenIconCpuMode.dylib','--output',out/'agent'),'agent/result.json')
    stage('nvt',script('generate_nvt_firmware_header.py','--firmware',inputs['nvt_firmware'],'--output',out/'nvt/EldenNvtFirmware.h'),'nvt/EldenNvtFirmware.h')
    if a.variant=='bluetooth':stage('generated',script('prepare_current_generated.py','--firmware',inputs['wcn_firmware'],'--library',out/'icon/EldenIconCpuMode.dylib','--agent',out/'agent/iconservicesagent','--output',out/'generated'),'generated/providers.json')
    stage('xnu',script('build_v91_xnu_objects.py','--xnu-source',out/'source/xnu','--xnu-repository',deps/'xnu','--payload-header',out/'agent/EldenIconCpuPayload.h','--output',out/'xnu','--jobs',a.jobs),'xnu/build/summary.json')
    stage('native',script('build_v91_native_archives.py','--xnu-source',out/'source/xnu','--xnu-repository',deps/'xnu','--dependencies',deps,'--nvt-header',out/'nvt/EldenNvtFirmware.h','--payload-header',out/'agent/EldenIconCpuPayload.h','--llvm-objcopy',inputs['llvm']/'llvm-objcopy','--output',out/'native','--jobs',a.jobs),'native/archives/firehose/result.json')
    generated_args=['--generated',out/'generated'] if a.variant=='bluetooth' else []
    stage('current-units',script('build_current_units.py','--variant',a.variant,'--xnu-source',out/'source/xnu','--dependencies',deps,'--icon-header',out/'agent/EldenIconCpuPayload.h','--nvt-header',out/'nvt/EldenNvtFirmware.h',*generated_args,'--llvm',inputs['llvm'],'--output',out/'units','--jobs',a.jobs),'units/result.json')
    stage('link',script('link_current_kernel.py','--variant',a.variant,'--xnu-objects',out/'xnu/build','--native-archives',out/'native/archives','--units',out/'units','--prelink-repository',deps/'XnuPrelink','--original-kernelcache',inputs['original_kernelcache'],'--output',out/'linked'),'linked/result.json')
    if a.target=='boot':
        stage('firmware',script('build_v91_firmware.py','--mu-root',inputs['mu_repository'],'--unixnu-root',inputs['unixnu_repository'],'--crypto-path',inputs['crypto'],'--llvm',inputs['llvm'],'--output',out/'firmware','--jobs',a.jobs),'firmware/result.json')
        stage('loader',script('build_v91_payload_loader.py','--mu-root',out/'firmware/project/third_party/mu_wide_platforms','--unixnu-root',out/'firmware/project/third_party/UniXNU','--llvm',inputs['llvm'],'--output',out/'loader'),'loader/result.json')
        stage('data',script('build_v91_data_seed.py','--os-image',inputs['os_image'],'--output',out/'data'),'data/result.json')
        ep={'candidate_sha256':read(out/'linked/result.json')['kernel_sha256'],'original_executable_sha256':read(ROOT/'profiles/v91/image-build/plan.json')['prelink']['original_executable_sha256']};(out/'entry-plan.json').write_text(json.dumps(ep,indent=2)+'\n')
        stage('entry',script('prepare_v91_entry.py','--kernel',out/'linked/kernel.macho','--plan',out/'entry-plan.json','--clang',inputs['llvm']/'clang','--output',out/'entry'),'entry/result.json')
        variant_args=['--wcn-report',out/'linked/result.json','--bt-runtime',out/'generated/runtime'] if a.variant=='bluetooth' else ['--hid-report',out/'linked/result.json']
        stage('boot',script('build_v91_boot.py','--portable-fat','--kernel',out/'entry/kernel.macho','--entry-report',out/'entry/result.json','--fd',out/'firmware/UEFI.fd','--loader',out/'loader/EldenPayloadLoader.efi','--data',out/'data/data.packed','--data-report',out/'data/result.json','--library',out/'icon/EldenIconCpuMode.dylib','--agent',out/'agent/iconservicesagent','--stock-boot',inputs['stock_boot'],'--avbtool',inputs['avbtool'],'--key',inputs['signing_key'],'--clang',inputs['llvm']/'clang',*variant_args,'--output',out/'boot'),'boot/result.json')
        stage('boot-audit',script('audit_v91_boot.py','--image',out/'boot/boot-signed.img','--entry-kernel',out/'entry/kernel.macho','--entry-report',out/'entry/result.json','--stock-boot',inputs['stock_boot'],'--parent-vbmeta',inputs['parent_vbmeta'],'--data-report',out/'data/result.json',*variant_args,'--output',out/'boot/audit.json'),'boot/audit.json')
    state['complete']=True;save();print('Completed '+a.target+'; no device operation performed',flush=True)
if __name__=='__main__':main()
