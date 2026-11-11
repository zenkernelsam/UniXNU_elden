"""Offline default-off/WCN7861 resource-only packaging, no radio writes."""
from pathlib import Path
import argparse,gzip,hashlib,json,re,shutil,struct,subprocess,sys,tomllib
from avb_guard import check_image
from audit_boot_candidate import Fat,PORT
sys.path.insert(0,str(PORT/'scripts'))
import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel
ROOT=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def wsl(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
def main():
    if sys.flags.optimize:raise SystemExit('Assertions must remain enabled')
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--variant',choices=['off','live'],required=True);a=p.parse_args()
    base=ROOT/'edl_work/hid-20261107/boot-signed.img';assert check_image(base,'boot_a').sha256=='7282b272395a3c7a9a099bad3ab1f10b20a6951320129a96373f3410a868a363'
    build=ROOT/'edl_work/wcn-native-20261108/final';out=build/a.variant;out.mkdir(exist_ok=True)
    b=base.read_bytes();raw=gzip.decompress(b[4096:4096+struct.unpack_from('<I',b,8)[0]]);before=Fat(raw[0x400080:])
    kernel=(build/'kernel.macho').read_bytes();assert sha(kernel)==json.loads((build/'derivative.json').read_text())['derivative_sha256']
    parse_kernel(kernel);symbols=maker.symbols(kernel)
    for name in ['_elden_bluetooth_inspect_start','_elden_wcn_hci_run','_elden_wcn_hci_prepare','_elden_wcn_hci_firmware_prepare','_pb_edl_segment','_pb_hci_complete','_elden_wcn_uart_backend_open','_elden_wcn_uart_backend_close','_elden_wcn_uart_acquire','_elden_wcn_uart_write','_elden_wcn_uart_read','_elden_wcn_uart_baud','_elden_wcn_startup_run','_elden_wcn_startup_prepare','_pb_geni_probe','_pb_h4_rx_byte']:
        assert name in symbols,name
    menu=before.read('/EFI/UniXNU/unixnu.toml').decode();old=tomllib.loads(menu);args=old['entry'][0]['args'];updated=args
    values={'elden-bluetooth-inspect':'0','elden-wcn-power':str({'off':0,'live':4}[a.variant])}
    for key,value in values.items():
        pattern=r'(?<!\S)'+key+r'=\S+'
        if re.search(pattern,updated):updated=re.sub(pattern,key+'='+value,updated)
        else:updated+=' '+key+'='+value
    for key in ['elden-data-write-reviewed','elden-data-initialize','elden-data-backup-reviewed']:
        assert not re.search(r'(?<!\S)'+key+r'=',updated)
    assert len(updated.encode())<1024;menu=menu.replace(args,updated,1)
    session=sha(kernel+menu.encode())[:16];menu=re.sub(r'elden-debug-session=[0-9a-f]{16}','elden-debug-session='+session,menu)
    cfg=tomllib.loads(menu);assert cfg['boot']==old['boot'] and cfg['boot']['default']=='xnu' and cfg['boot']['timeout']==8
    (out/'unixnu.toml').write_text(menu,encoding='utf-8',newline='\n');(out/'kernel.macho').write_bytes(kernel)
    payload=out/'payload.img';payload.write_bytes(raw[0x400080:])
    for src,dest in [('kernel.macho','Payloads/kernel.macho'),('unixnu.toml','unixnu.toml')]:
        subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','mcopy','-o','-i',wsl(payload),wsl(out/src),'::/EFI/UniXNU/'+dest],check=True)
    after=Fat(payload.read_bytes());unchanged={}
    for path in ['Payloads/kernel.macho.adt','Payloads/kernel.macho.trustcache','Payloads/data.packed','EldenPayloadLoader.efi']:
        content=after.read('/EFI/UniXNU/'+path);assert content==before.read('/EFI/UniXNU/'+path);unchanged[path]=sha(content)
    assert after.read('/EFI/UniXNU/Payloads/kernel.macho')==kernel
    compressed=gzip.compress(raw[:0x400080]+payload.read_bytes(),mtime=0);head=bytearray(b[:4096]);struct.pack_into('<I',head,8,len(compressed))
    unsigned=bytes(head)+compressed;unsigned+=bytes(-len(unsigned)%4096);(out/'boot.img').write_bytes(unsigned)
    shutil.copyfile(build/'hid-abi.json',out/'hid-abi.json');shutil.copyfile(build/'kernel.json',out/'source-plan.json')
    packet={'variant':a.variant,'session':session,'kernel_sha256':sha(kernel),'unsigned_sha256':sha(unsigned),'flags':values,
            'unchanged':unchanged,'radio_writes_enabled':a.variant=='live','device_tested':False}
    (out/'packet.json').write_text(json.dumps(packet,indent=2)+'\n',encoding='utf-8');print(json.dumps(packet,indent=2))
if __name__=='__main__':main()
