"""Build actual ADC/WCN gate consumers; not a complete kernel or flashable image."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,re,shutil,subprocess,sys
from replay_xnu_objects import global_symbols,sections
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def loadpatch():
    sp=importlib.util.spec_from_file_location('spmi_gate_patch',ROOT/'drivers/EldenSpmi/SourcePatch.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);return m.apply
def main():
    if sys.flags.optimize:raise SystemExit('Validation requires assertions')
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ('baseline','llvm','output'):ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();base=a.baseline.resolve();out=a.output.resolve()
    assert sha(base/'kernel.macho')=='0db8f52b3d85026f1a85a000ab665c47cf084a340e87c09f556900b5355ee848'
    if out.exists():raise ValueError('Fresh output required')
    out.mkdir(parents=True);src=out/'source';shutil.copytree(base/'source',src);patch=loadpatch()
    adc=ROOT/'profiles/v91/sources/power'
    for n in ('Spmi.h','Adc.h','EldenAdcResources.h'):shutil.copyfile(adc/n,src/n)
    (src/'Spmi.c').write_bytes(patch('Spmi.c',(adc/'Spmi.c').read_bytes()))
    for n in ('BtPmic.c','WcnPowerNative.cpp'):(src/n).write_bytes(patch(n,(base/'source'/n).read_bytes()))
    for n in ('TransactionGate.c','TransactionGate.h'):shutil.copyfile(ROOT/'drivers/EldenSpmi'/n,src/n)
    prior=json.loads((base/'object-audit.json').read_text());records={}
    for name in ('TransactionGate','Spmi','BtPmic','WcnPowerNative'):
        target=out/(name+'.o');cwd=out
        if name=='WcnPowerNative':
            command=list(prior[name]['command']);i=command.index('-ivfsoverlay')+1;ov=json.loads(Path(command[i]).read_text());assert ov.get('fallthrough') is False
            old=str(base/'source');new=str(src)
            for row in ov['roots']:
                if row['name'].startswith(old+'/'):row['name']=row['name'].replace(old,new,1)
                if row['external-contents'].startswith(old+'/'):row['external-contents']=row['external-contents'].replace(old,new,1)
            known={x['name'] for x in ov['roots']}
            for p in src.iterdir():
                if p.is_file() and str(p) not in known:ov['roots'].append({'type':'file','name':str(p),'external-contents':str(p)})
            overlay=out/'wcn-overlay.json';overlay.write_text(json.dumps(ov,indent=2)+'\n');command=[x.replace(old,new) for x in command];command[i]=str(overlay);command[command.index('-o')+1]=str(target)
            # The captured command includes relative forced headers from this
            # exact compile directory; resolve it from the closed VFS mapping.
            # This WCN unit was compiled with the v91 CpuPerfSurvey context.
            dirs=[v.split('=',1)[1] for v in command if v.startswith('-fdebug-compilation-dir=')];assert len(dirs)==1
            virtual=dirs[0]+'/meta_features.h'
            matched=[r for r in ov['roots'] if r['name']==virtual];assert len(matched)==1
            cwd=Path(matched[0]['external-contents']).parent
        else:
            command=['xcrun','--sdk','iphoneos','clang','-target','arm64-apple-ios17.0','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-common','-mgeneral-regs-only','-DKERNEL','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I',str(src),'-c',str(src/(name+'.c')),'-o',str(target)]
        env=dict(os.environ,CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option');env.pop('SDKROOT',None)
        with (out/(name+'.log')).open('x') as log:subprocess.run(command,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,env=env,check=True)
        text=subprocess.check_output([str(a.llvm/'llvm-objdump'),'-dr',str(target)],text=True);(out/(name+'.asm')).write_text(text)
        assert not re.search(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+.*\b[qvdshb]\d+(?:\b|\.)',text,re.M)
        assert not any(k.endswith('/__mod_init_func') for k in sections(target))
        syms=global_symbols(target);exports=sorted(n for n,v in syms.items() if v[0]&0x0e);imports=sorted(n for n,v in syms.items() if not v[0]&0x0e)
        gate={'_elden_spmi_transaction_acquire','_elden_spmi_transaction_release','_elden_spmi_transaction_state'}
        if name=='TransactionGate':assert set(exports)==gate
        else:assert gate-{'_elden_spmi_transaction_state'}<=set(imports)
        if name in ('BtPmic','WcnPowerNative'):assert set(exports)==set(prior[name]['exports'])
        records[name]={'source_sha256':sha(src/(name+('.cpp' if name=='WcnPowerNative' else '.c'))),'object_sha256':sha(target),'exports':exports,'imports':imports,'scalar':True,'command':command}
    result={'components':records,'shared_gate_one_provider':True,'actual_adc_and_bt_consumers':True,'full_kernel_linked':False,'hardware_tested':False,'baseline_sha256':sha(base/'kernel.macho'),'patcher_sha256':sha(ROOT/'drivers/EldenSpmi/SourcePatch.py')}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='components'},indent=2))
if __name__=='__main__':main()
