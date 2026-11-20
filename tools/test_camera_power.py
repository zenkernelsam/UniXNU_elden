"""Test actual WL2868C ledger and original two-chip GENI/shared-owner paths."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    report=json.loads((a.source/'source.json').read_text())
    for name,digest in report['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Prepared actual source changed: '+name)
    def run(command,label):
        r=subprocess.run(list(map(str,command)),capture_output=True,timeout=120)
        (out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
        return r.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=undefined','-I',a.source]
    run(['xcrun','clang','-std=c11',*flags,a.source/'Power.c',DRIVER/'TestPower.c','-o',out/'power-test'],'power-build')
    power=run([out/'power-test'],'power-run')
    objects=[]
    for name,path in [('Platform',a.source/'Platform.c'),('GeniI2c',a.source/'GeniI2c.c'),('Aw99706',a.source/'Aw99706.c'),
                      ('Power',a.source/'Power.c'),('PowerBus',a.source/'PowerBus.c'),('Peer',DRIVER/'TestI2c10Peer.c')]:
        obj=out/(name+'.o');run(['xcrun','clang','-std=c11',*flags,'-c',path,'-o',obj],name+'-host');objects.append(obj)
    run(['xcrun','clang++','-std=c++17',*flags,DRIVER/'TestSharedBus.cpp',*objects,'-o',out/'shared-test'],'shared-build')
    cases=('normal','race','close-first','retirement-fail','release-error','nack','cancel','abort','allocation-fail','deny','irq','power-session','power-id-fail')
    shared={name:run([out/'shared-test',name],'shared-'+name) for name in cases}
    # Untouched original backlight tests execute against the derived transfer
    # and original GENI core as an independent wire/clock/error regression.
    run(['xcrun','clang','-std=c11',*flags,a.source/'Platform.c',a.source/'GeniI2c.c',a.source/'Aw99706.c',
         ROOT/'drivers/EldenBacklight/TestPlatform.c','-o',out/'backlight-test'],'legacy-build')
    original=run([out/'backlight-test'],'legacy-run')
    record={'passed':True,'power_output':power,'shared_cases':shared,'original_backlight_output':original,
        'prepared_source_sha256':sha(a.source/'source.json'),'one_original_GENI_engine':True,
        'actual_shared_methods_and_WL_session':True,'registers_ownership_IOKit_modeled':True,'hardware':False}
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    print(power+original+''.join(shared.values()))
if __name__=='__main__':main()
