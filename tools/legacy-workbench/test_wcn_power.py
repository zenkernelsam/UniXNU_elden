"""Offline exact power sequence, PLL/OL-CPR and shared TCS timeout fence."""
from pathlib import Path
import hashlib,json,re,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1];PORT=Path('D:/Desktop/Git/UniXNU_elden')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def wp(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
def main():
    if sys.flags.optimize:raise SystemExit('Assertions must remain enabled')
    out=ROOT/'edl_work/wcn-power-20261108';out.mkdir(exist_ok=True)
    ref=ROOT/'research/wcn-power-20261108/test-reference'
    name='drivers/Adreno830/tests/test_sm8750_rpmh.c';pin='c888efd3ce1b817d13f28783a77cf689cb2255c9'
    f=ref/'tests/test_sm8750_rpmh.c';f.parent.mkdir(parents=True,exist_ok=True)
    b=subprocess.check_output(['git','-C',str(ROOT/'research/UniXNU-Pagani-Drivers'),'show',pin+':'+name])
    if f.exists():assert f.read_bytes()==b
    else:f.write_bytes(b)
    h=ref/'platform/sm8750/Sm8750Rpmh.h';h.parent.mkdir(parents=True,exist_ok=True)
    h.write_bytes((ROOT/'research/wcn-power-20261108/pagani/Sm8750Rpmh.h').read_bytes())
    src=ROOT/'research/wcn-power-20261108/pagani/Sm8750Rpmh.c'
    text=src.read_text();old='    rc = transact(&ctx, cmds, n, wait_mask);\n    Sm8750RpmhDrv2Release();'
    assert text.count(old)==1
    text=text.replace(old,'    rc = transact(&ctx, cmds, n, wait_mask);\n    if (!rc || result->step < Sm8750RpmhStepTrigger) Sm8750RpmhDrv2Release();')
    derived=out/'Sm8750Rpmh.wcn.c';derived.write_text(text,encoding='utf-8',newline='\n')
    base=['wsl','-d','Ubuntu-22.04','-u','root','--','cc','-std=c11','-Wall','-Wextra','-Werror',
          '-Wno-unused-function','-Wno-misleading-indentation','-O1','-g','-pthread',
          '-fsanitize=address,undefined','-fno-omit-frame-pointer']
    started=time.monotonic();runs=[]
    scenarios=[('test-power',[PORT/'drivers/EldenBluetooth/WcnPower.c',PORT/'drivers/EldenBluetooth/TestWcnPower.c'],[]),
        ('test-rpm-fence',[derived,PORT/'drivers/EldenBluetooth/TestWcnRpmFence.c'],
         ['-I',wp(ref/'tests'),'-I',wp(ROOT/'research/wcn-power-20261108/pagani')])]
    for name,files,includes in scenarios:
        subprocess.run(base+includes+[wp(f) for f in files]+['-o',wp(out/name)],check=True,timeout=55)
        r=subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','env','ASAN_OPTIONS=detect_leaks=1',
            'UBSAN_OPTIONS=halt_on_error=1',wp(out/name)],capture_output=True,text=True,timeout=55)
        print(r.stdout,end='');print(r.stderr,end='',file=sys.stderr);r.check_returncode()
        runs.append({'name':name,'output':r.stdout,'source_sha256':{str(f):sha(f) for f in files}})
    report={'accepted':True,'asan':True,'ubsan':True,'hardware_tested':False,
            'seconds':time.monotonic()-started,'tests':runs,
            'rpmh_original_sha256':sha(src),'rpmh_derived_sha256':sha(derived)}
    (out/'host-tests.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
