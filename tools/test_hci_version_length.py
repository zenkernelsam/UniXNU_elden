"""Reproduce the real EDL length rejection and test its narrowly scoped fix."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def lp(p):
    p=p.resolve()
    if len(p.drive)!=2:raise ValueError('Windows drive required')
    return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
def main():
    if sys.flags.optimize:raise SystemExit('Assertions required')
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ('dependency','firmware','output'):ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();out=a.output.resolve();src=ROOT/'profiles/v114-hci-length/sources';bt=ROOT/'drivers/EldenBluetooth'
    if out.exists():raise ValueError('Fresh output required')
    out.mkdir(parents=True);ref=out/'reference';ref.mkdir();plan=json.loads((ROOT/'profiles/v110-bt-fifo/plan.json').read_text())
    for n in ('BtH4.c','BtH4.h','BtTlv.c','BtTlv.h','BtUtil.h'):
        item=plan['dependency']['files'][n];d=subprocess.check_output(['git','-C',str(a.dependency),'show',plan['dependency']['revision']+':'+item['path']]);assert hashlib.sha256(d).hexdigest()==item['sha256'];(ref/n).write_bytes(d)
    names=['qca/brhperifw20.tlv','qca/brhperinv20.bin','qca/tmel_peach_20.elf','qca/brhbtfw20.tlv','qca/lenovo/tb322fc/brhbtnv20.bin']
    for item in plan['firmware']:assert sha(a.firmware/item['file'])==item['sha256']
    raw=bytes.fromhex('040e120100fc00190c21000000586d000200022140');assert raw[8]==len(raw)-9==12
    results={}
    for mode in ('old','fixed'):
        protocol=bt/'Wcn7861Hci.c' if mode=='old' else src/'Wcn7861Hci.c'
        files=[src/'TestWcn7861Hci.c',protocol,bt/'Wcn7861Startup.c',bt/'Wcn7861Peripheral.c',ref/'BtH4.c',ref/'BtTlv.c']
        cmd=['wsl','-d','Ubuntu-22.04','-u','root','--','cc','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer']
        for p in (src,bt,ref):cmd+=['-I',lp(p)]
        cmd += [lp(p) for p in files]+['-o',lp(out/('test-'+mode))]
        r=subprocess.run(cmd,capture_output=True,timeout=90);(out/(mode+'-build.log')).write_bytes(r.stdout+r.stderr);r.check_returncode()
        run=['wsl','-d','Ubuntu-22.04','-u','root','--','env','ASAN_OPTIONS=detect_leaks=1','UBSAN_OPTIONS=halt_on_error=1',cmd[-1]]+[lp(a.firmware/n) for n in names]
        r=subprocess.run(run,capture_output=True,timeout=90);(out/(mode+'-run.log')).write_bytes(r.stdout+r.stderr)
        if mode=='old':assert r.returncode!=0 and b'!rc&&hci.ready' in r.stderr
        else:r.check_returncode();assert b'PASS 25 HCI scenarios' in r.stdout
        results[mode]={'returncode':r.returncode,'output':(r.stdout+r.stderr).decode(errors='replace'),'source_sha256':sha(protocol),'command':cmd,'run':run}
        print(mode+': '+results[mode]['output'].strip())
    report={'actual_v113_version_hex':raw.hex(),'real_reply_fields_in_success_fixture':True,'unchanged_status_and_identity_checks':True,'results':results,'hardware_verified':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
