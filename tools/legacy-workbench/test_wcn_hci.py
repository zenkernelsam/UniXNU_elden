"""Offline ASan/UBSan test against all five immutable TB322FC assets."""
from pathlib import Path
import hashlib,json,re,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1]
PORT=Path('D:/Desktop/Git/UniXNU_elden')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def wp(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
def main():
    if sys.flags.optimize:raise SystemExit('Assertions must remain enabled')
    out=ROOT/'edl_work/hci-wcn7861-20261108';out.mkdir(exist_ok=True)
    reference=ROOT/'research/wcn7861-hci-20261108/pagani'
    previous=ROOT/'research/wcn7861-runtime-20261108'
    reference.mkdir(parents=True,exist_ok=True)
    m=json.loads((previous/'pagani-source.json').read_text())
    for name,digest in m['files'].items():
        src=previous/'pagani'/name;assert sha(src)==digest
        target=reference/name
        if target.exists():assert sha(target)==digest
        else:target.write_bytes(src.read_bytes())
    (reference.parent/'pagani-source.json').write_text(json.dumps(m,indent=2)+'\n',encoding='utf-8')
    inputs=ROOT/'research/armada-bt-firmware-20261108'
    manifest=json.loads((inputs/'manifest.json').read_text())
    names=['qca/brhperifw20.tlv','qca/brhperinv20.bin','qca/tmel_peach_20.elf',
           'qca/brhbtfw20.tlv','qca/lenovo/tb322fc/brhbtnv20.bin']
    firmware={}
    for name in names:
        f=inputs/name;item=next(x for x in manifest['files'] if x['file']==name)
        assert f.stat().st_size==item['bytes'] and sha(f)==item['sha256']
        firmware[name]={'bytes':item['bytes'],'sha256':sha(f)}
    source=PORT/'drivers/EldenBluetooth'
    files=[source/'Wcn7861Hci.c',source/'Wcn7861Startup.c',source/'Wcn7861Peripheral.c',
           reference/'BtH4.c',reference/'BtTlv.c',source/'TestWcn7861Hci.c']
    command=['wsl','-d','Ubuntu-22.04','-u','root','--','cc','-std=c11','-Wall','-Wextra',
             '-Werror','-Wno-misleading-indentation','-O1','-g','-fsanitize=address,undefined',
             '-fno-omit-frame-pointer','-I',wp(reference),*[wp(p) for p in files],'-o',wp(out/'test-hci')]
    started=time.monotonic()
    subprocess.run(command,check=True,timeout=55)
    run=['wsl','-d','Ubuntu-22.04','-u','root','--','env','ASAN_OPTIONS=detect_leaks=1',
         'UBSAN_OPTIONS=halt_on_error=1',wp(out/'test-hci'),*[wp(inputs/n) for n in names]]
    result=subprocess.run(run,check=True,capture_output=True,text=True,timeout=55)
    print(result.stdout,end='')
    match=re.fullmatch(r'PASS (\d+) HCI scenarios, (\d+) five-firmware byte/transport/boundary assertions\n',result.stdout)
    assert match and all(sha(inputs/n)==firmware[n]['sha256'] for n in names)
    record={'accepted':True,'hardware_tested':False,'scenario_count':int(match[1]),
            'assertions_including_byte_comparisons':int(match[2]),'asan':True,'ubsan':True,
            'seconds':time.monotonic()-started,'firmware':firmware,
            'source_sha256':{str(f):sha(f) for f in files},'command':command}
    (out/'host-tests.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
