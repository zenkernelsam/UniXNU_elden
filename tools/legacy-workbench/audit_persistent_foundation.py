"""Independent signature/assets/ABI and absence-of-write-authorization checks."""
from pathlib import Path
import gzip,hashlib,json,struct,sys,tomllib
from avb_guard import check_image,metadata
from audit_boot_candidate import Fat,PORT
sys.path.insert(0,str(PORT/'scripts'))
import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel,adt_walk
ROOT=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def raw(path):
    b=path.read_bytes();return gzip.decompress(b[4096:4096+struct.unpack_from('<I',b,8)[0]])
def main():
    if sys.flags.optimize:raise SystemExit('Assertions must remain enabled')
    out=ROOT/'edl_work/persistent-data-20261108/final';b=(out/'kernel.macho').read_bytes();parse_kernel(b);symbols=maker.symbols(b)
    plan=json.loads((out/'kernel.json').read_text());original=bytearray(b);amfi=maker.locate(b,0xfffffff004aaecfc)[1]
    assert struct.unpack_from('<I',b,amfi)[0]==0x17ffffd4;struct.pack_into('<I',original,amfi,0xaa1503e0)
    for module,digest in plan['original_executable_sha256'].items():
        e=next(e for e in plan['extents'] if e['module']==module and e['segment']=='__TEXT_EXEC');a=maker.locate(b,e['va'])[1]
        assert sha(original[a:a+e['size']])==digest,module
    abi=json.loads((out/'hid-abi.json').read_text());assert abi['original_slots']==207 and abi['original_instance_size']==208
    for name,va in abi['bindings'].items():assert symbols[name]==va
    for name in ['_elden_data_command_allowed','_elden_data_gpt_pair','_elden_data_reboot_fence','_elden_data_inspect_start',
                 '_native_ufs_store_write','_native_ufs_store_sync','_elden_data_fence_close']:
        assert name in symbols,name
    for text in [b'ELDEN_DATA_LAYOUT valid=',b'ELDEN_DATA RESET_HOLD',b'ELDEN_DATA PANIC_HOLD',b'ELDEN_DATA REBOOT_FENCE durable=1']:
        assert text in b,text
    before=raw(ROOT/'edl_work/hid-20261107/boot-signed.img');bf=Fat(before[0x400080:]);reports=[]
    for variant in ['off','inspect']:
        d=out/variant;a=check_image(d/'boot-signed.img','boot_a');assert a.index==1738713600 and a.location==0 and a.bytes==100663296
        assert metadata(d/'boot-signed.img')['props']==metadata(ROOT/'signing/phase5_resign/boot_signed.img')['props']
        r=raw(d/'boot-signed.img');assert r[:0x400080]==before[:0x400080];f=Fat(r[0x400080:]);assert f.read('/EFI/UniXNU/Payloads/kernel.macho')==b
        packet=json.loads((d/'packet.json').read_text());cfg=tomllib.loads(f.read('/EFI/UniXNU/unixnu.toml').decode())
        assert cfg['boot']['default']=='xnu' and cfg['boot']['timeout']==8
        for e in cfg['entry']:
            args=e.get('args','').split();assert len(e.get('args','').encode())<1024
            for key in ['sheng-data-persist','elden-data-write-reviewed','elden-data-initialize','elden-data-backup-reviewed']:
                matches=[w for w in args if w.startswith(key+'=')];assert len(matches)<=1
                assert all(w==key+'=0' for w in matches),matches
        args=cfg['entry'][0]['args'];assert 'elden-debug-session='+packet['session'] in args and 'elden-hid=1' in args
        for path,digest in packet['unchanged'].items():
            data=f.read('/EFI/UniXNU/'+path);assert data==bf.read('/EFI/UniXNU/'+path) and sha(data)==digest
        assert adt_walk(f.read('/EFI/UniXNU/Payloads/kernel.macho.adt'))['window']==0x180000000
        reports.append({'variant':variant,'image_sha256':a.sha256,'session':packet['session'],'index':a.index,'location':a.location})
    result={'accepted':True,'kernel_sha256':sha(b),'images':reports,'original_modules':len(plan['original_executable_sha256']),
            'hid_slots':207,'native_hid_bindings':len(abi['bindings']),'persistent_write_authorization_absent':True,'hardware_tested':False}
    (out/'independent-audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
