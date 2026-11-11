"""Offline UART/FIFO/CTS/RFR/clock model under the complete five-asset startup."""
from pathlib import Path
import hashlib,json,re,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1]
PORT=Path('D:/Desktop/Git/UniXNU_elden')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def wp(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
def main():
    if sys.flags.optimize:raise SystemExit('Assertions must remain enabled')
    out=ROOT/'edl_work/wcn-uart-20261108';out.mkdir(exist_ok=True)
    old=ROOT/'research/wcn7861-hci-20261108'
    ref=ROOT/'research/wcn7861-uart-20261108/pagani';ref.mkdir(exist_ok=True)
    manifest=json.loads((old/'pagani-source.json').read_text())
    for n,digest in manifest['files'].items():
        f=old/'pagani'/n;assert sha(f)==digest;target=ref/n
        if target.exists():assert sha(target)==digest
        else:target.write_bytes(f.read_bytes())
    (ref.parent/'pagani-source.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    src=PORT/'drivers/EldenBluetooth'
    files=[src/n for n in ['UartPlatform.c','Wcn7861Hci.c','Wcn7861Startup.c',
                           'Wcn7861Peripheral.c','TestUartPlatform.c']]
    files += [ref/n for n in ['BtGeni.c','BtH4.c','BtTlv.c']]
    cmd=['wsl','-d','Ubuntu-22.04','-u','root','--','cc','-std=c11','-O1','-g',
         '-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined',
         '-fno-omit-frame-pointer','-I',wp(ref),'-I',wp(PORT/'package'),
         '-I',wp(PORT/'soc/sm8750/include'),*[wp(f) for f in files],'-o',wp(out/'test-uart')]
    start=time.monotonic();subprocess.run(cmd,check=True,timeout=55)
    inputs=ROOT/'research/armada-bt-firmware-20261108'
    names=['qca/brhperifw20.tlv','qca/brhperinv20.bin','qca/tmel_peach_20.elf',
           'qca/brhbtfw20.tlv','qca/lenovo/tb322fc/brhbtnv20.bin']
    before={n:sha(inputs/n) for n in names}
    run=['wsl','-d','Ubuntu-22.04','-u','root','--','env','ASAN_OPTIONS=detect_leaks=1',
         'UBSAN_OPTIONS=halt_on_error=1',wp(out/'test-uart'),*[wp(inputs/n) for n in names]]
    result=subprocess.run(run,capture_output=True,text=True,timeout=55)
    print(result.stdout,end='');print(result.stderr,end='',file=sys.stderr);result.check_returncode()
    m=re.fullmatch(r'PASS (\d+) UART/platform scenarios, (\d+) FIFO/wire/clock/lease assertions\n',result.stdout)
    assert m and before=={n:sha(inputs/n) for n in names}
    record={'accepted':True,'hardware_tested':False,'scenario_count':int(m[1]),
            'assertions_including_byte_comparisons':int(m[2]),'asan':True,'ubsan':True,
            'source_sha256':{str(f):sha(f) for f in files},'firmware':before,
            'seconds':time.monotonic()-start,'command':cmd}
    (out/'host-tests.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
