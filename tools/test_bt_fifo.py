"""Run v110's full FIFO/protocol fixture against fixed dependencies and firmware."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def linux(p):
    p=p.resolve()
    if len(p.drive)!=2:raise ValueError('Windows path required')
    return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
def main():
    if sys.flags.optimize:raise SystemExit('Assertions must stay enabled')
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ('dependency','firmware','output'):ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();out=a.output.resolve();profile=ROOT/'profiles/v110-bt-fifo';plan=json.loads((profile/'plan.json').read_text())
    if out.exists():raise ValueError('Fresh output required')
    out.mkdir(parents=True);ref=out/'reference';ref.mkdir()
    for n,item in plan['dependency']['files'].items():
        if not n.startswith(('BtGeni','BtH4','BtTlv','BtUtil')):continue
        data=subprocess.check_output(['git','-C',str(a.dependency),'show',plan['dependency']['revision']+':'+item['path']]);assert hashlib.sha256(data).hexdigest()==item['sha256'];(ref/n).write_bytes(data)
    for item in plan['firmware']:assert sha(a.firmware/item['file'])==item['sha256']
    src=profile/'sources';bt=ROOT/'drivers/EldenBluetooth'
    files=[src/'UartPlatform.c',src/'TestUartPlatform.c']+[bt/n for n in ('Wcn7861Hci.c','Wcn7861Startup.c','Wcn7861Peripheral.c')]+[ref/n for n in ('BtGeni.c','BtH4.c','BtTlv.c')]
    cmd=['wsl','-d','Ubuntu-22.04','-u','root','--','cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    for p in (src,bt,ref,ROOT/'package',ROOT/'soc/sm8750/include'):cmd+=['-I',linux(p)]
    cmd += [linux(p) for p in files]+['-o',linux(out/'test-uart')]
    r=subprocess.run(cmd,capture_output=True,timeout=90);(out/'build.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    names=['qca/brhperifw20.tlv','qca/brhperinv20.bin','qca/tmel_peach_20.elf','qca/brhbtfw20.tlv','qca/lenovo/tb322fc/brhbtnv20.bin']
    run=['wsl','-d','Ubuntu-22.04','-u','root','--','env','ASAN_OPTIONS=detect_leaks=1','UBSAN_OPTIONS=halt_on_error=1',linux(out/'test-uart')]+[linux(a.firmware/n) for n in names]
    r=subprocess.run(run,capture_output=True,timeout=90);(out/'run.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    result={'passed':True,'hardware_tested':False,'command':cmd,'run':run,'result':r.stdout.decode(),'sources':{str(p):sha(p) for p in files}}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result['result'])
if __name__=='__main__':main()
