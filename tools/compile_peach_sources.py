"""Compile the six changed Peach C units against the pinned Ath12kWifi shim.

This checks source/ABI preparation only, not the whole125-object stack, a
kernel link or hardware. The real power-sequencer symbol must stay unresolved.
"""
import argparse,gzip,hashlib,io,json,re,shutil,subprocess,sys,tarfile
from pathlib import Path
from replay_xnu_objects import global_symbols,sections
ROOT=Path(__file__).resolve().parents[1]
REV='5f7f5a8a9f02f7ab45ed10a2f226e348c519a838'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('component-repository','linux-archive','backport','llvm','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve()
    if out.exists():raise ValueError('New output directory required')
    linux=json.loads((ROOT/'drivers/EldenWifi/peach/linux-input.json').read_text());raw=gzip.decompress(a.linux_archive.read_bytes())
    if hashlib.sha256(raw).hexdigest()!=linux['tar_sha256']:raise ValueError('Wrong Linux source archive')
    backport=json.loads((a.backport/'result.json').read_text());base='drivers/net/wireless/ath/ath12k/'
    if backport['base_commit']!=linux['commit'] or backport['required_compile_defines']!={'CONFIG_POWER_SEQUENCING_QCOM_WCN':1}:raise ValueError('Wrong backport contract')
    for name,info in backport['files'].items():
        if sha(a.backport/'tree'/name)!=info['output_sha256']:raise ValueError('Backport source changed')
    out.mkdir(parents=True);component=out/'component';component.mkdir()
    packed=subprocess.check_output(['git','-c','core.autocrlf=false','-c','core.eol=lf','-C',str(a.component_repository),'archive',REV])
    with tarfile.open(fileobj=io.BytesIO(packed)) as t:t.extractall(component,filter='data')
    original=component/'deps/linux';original.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(raw)) as t:t.extractall(original,filter='data')
    # pin-headers only removes out/pinned, which is absent in this new snapshot.
    # No existing reference/generated output is passed to the upstream tool.
    if (component/'out').exists():raise ValueError('Reference snapshot unexpectedly contains build output')
    subprocess.run(['bash',str(component/'tools/pin-headers.sh')],check=True)
    generated=component/'out/ath12k-src';generated.mkdir();(generated/'wifi7').mkdir()
    for folder in ('','wifi7'):
        for f in (original/base/folder).iterdir():
            if f.suffix in ('.c','.h') and f.name!='trace.h':shutil.copyfile(f,generated/folder/f.name)
    for name in backport['files']:
        if name.startswith(base):shutil.copyfile(a.backport/'tree'/name,generated/name.removeprefix(base))
        else:
            target=component/'out/pinned'/name.removeprefix('include/');target.parent.mkdir(parents=True,exist_ok=True)
            if target.exists():raise ValueError('New power header would overwrite an existing shim')
            shutil.copyfile(a.backport/'tree'/name,target)
    traces=set()
    for f in generated.rglob('*'):
        if f.suffix in ('.c','.h'):traces.update(re.findall(r'trace_ath12k_[a-z_0-9]+',f.read_text()))
    (generated/'trace.h').write_text('#ifndef LX_ATH12K_TRACE_H\n#define LX_ATH12K_TRACE_H\n'+''.join('#define '+n+'(...) ((void)0)\n' for n in sorted(traces))+'#endif\n')
    clang=subprocess.check_output(['xcrun','--sdk','iphoneos','--find','clang'],text=True).strip()
    resource=subprocess.check_output([clang,'-print-resource-dir'],text=True).strip()+'/include'
    flags=['-target','arm64-apple-ios17.0','-mkernel','-O2','-g0','-ffreestanding','-fno-builtin','-fno-common','-fno-strict-aliasing','-fwrapv','-fno-stack-protector','-fno-delete-null-pointer-checks','-mgeneral-regs-only','-std=gnu11',
           '-DCONFIG_ATH12K_DEBUG','-DCONFIG_CFG80211=1','-DCONFIG_MAC80211=1','-DCONFIG_MAC80211_STA_HASH_MAX_SIZE=0','-DCONFIG_MAC80211_RC_DEFAULT=""','-DCONFIG_POWER_SEQUENCING_QCOM_WCN=1',
           '-nostdinc','-isystem',str(component/'shim/libc'),'-isystem',resource,'-include','lx_pci.h']
    for folder in ('shim/include','include','out/pinned','src','out','out/ath12k-src','out/ath12k-src/wifi7'):flags+=['-I',str(component/folder)]
    objects=out/'objects';objects.mkdir();logs=out/'logs';logs.mkdir();records={}
    for name in ('core.c','pci.c','qmi.c','wifi7/hal.c','wifi7/hw.c','wifi7/pci.c'):
        source=generated/name;tag=name.replace('/','-');obj=objects/(tag+'.o');command=[clang,'-c',*flags,str(source),'-o',str(obj)]
        result=subprocess.run(command,capture_output=True,text=True);(logs/(tag+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(name+' compile failed:\n'+result.stderr[-5000:])
        asm=subprocess.check_output([str(a.llvm/'llvm-objdump'),'--disassemble','--reloc',str(obj)],text=True);(logs/(tag+'.asm')).write_text(asm)
        ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',asm,re.M)
        if not ops or any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',v) for v in ops):raise ValueError('Unexpected empty/SIMD object')
        if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic initializer found')
        syms=global_symbols(obj);records[name]={'source_sha256':sha(source),'object_sha256':sha(obj),'imports':sorted(n for n,v in syms.items() if not v[0]&0x0e),'scalar':True,'command':command}
    if '_pwrseq_qcom_wcn_set_ol_cpr' not in records['qmi.c']['imports']:raise ValueError('Real power-provider dependency was compiled away')
    result={'component_revision':REV,'linux_tar_sha256':linux['tar_sha256'],'backport_report_sha256':sha(a.backport/'result.json'),'objects':records,'pwrseq_external_required':True,
            'all_stack_objects_built':False,'kernel_linked':False,'native_wlan_gateway_implemented':False,'hardware_operations':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'compiled_units':6,'pwrseq_external_required':True,'kernel_linked':False}))
if __name__=='__main__':main()
