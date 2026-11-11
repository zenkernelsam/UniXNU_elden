"""Independent signed power-mode audit; does not communicate with a device."""
from pathlib import Path
import gzip,hashlib,json,struct,sys,tomllib
from audit_boot_candidate import Fat,PORT
from avb_guard import check_image,metadata
sys.path.insert(0,str(PORT/'scripts'))
import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel,adt_walk
ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def unpack(path):
    image=path.read_bytes();return gzip.decompress(image[4096:4096+struct.unpack_from('<I',image,8)[0]])
def main():
    if sys.flags.optimize:raise SystemExit('Assertions must remain enabled')
    root=ROOT/'edl_work/power-runtime-20261108';base=ROOT/'edl_work/hid-20261107';original=unpack(base/'boot-signed.img')
    before=Fat(original[0x400080:]);reports=[];same=None
    expected={'firmware':(1,0,0),'survey':(0,1,0),'pmic':(0,2,1)}
    for variant,(check,adsp,pmic) in expected.items():
        work=root/variant;packet=json.loads((work/'packet.json').read_text());a=check_image(work/'boot-signed.img','boot_a')
        assert a.index==1738713600 and a.location==0 and a.bytes==100663296
        assert metadata(work/'boot-signed.img')['props']==metadata(ROOT/'signing/phase5_resign/boot_signed.img')['props']
        raw=unpack(work/'boot-signed.img');assert raw[:0x400080]==original[:0x400080]
        fat=Fat(raw[0x400080:]);kernel=fat.read('/EFI/UniXNU/Payloads/kernel.macho')
        assert sha(kernel)==packet['kernel_sha256']==sha((root/'kernel.macho').read_bytes())
        if same is None:same=kernel
        else:assert same==kernel
        for path,digest in packet['unchanged'].items():
            blob=fat.read('/EFI/UniXNU/'+path);assert blob==before.read('/EFI/UniXNU/'+path) and sha(blob)==digest
        cfg=tomllib.loads(fat.read('/EFI/UniXNU/unixnu.toml').decode());assert cfg['boot']['default']=='xnu' and cfg['boot']['timeout']==8
        args=cfg['entry'][0]['args'].split();flags={}
        for word in args:
            if '=' in word:
                key,value=word.split('=',1);assert key not in flags;flags[key]=value
        assert tuple(int(flags[k]) for k in ['elden-modem-fw-check','elden-adsp','elden-pmic'])==(check,adsp,pmic)
        assert flags['elden-debug-session']==packet['session'] and flags['elden-hid']=='1'
        assert all(len(e.get('args','').encode())<1024 for e in cfg['entry'])
        assert adt_walk(fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt'))['window']==0x180000000
        parse_kernel(kernel);symbols=maker.symbols(kernel)
        for name in ['_native_ufs_part_find','_native_ufs_part_read','_sheng_adsp_probe','_sheng_adsp_send_epoch','_elden_pmic_client_start','_elden_charge_poll_next','_elden_hid_request_home']:
            assert name in symbols,name
        for text in [b'ELDEN_FIRMWARE_CHECK',b'ELDEN_PMIC cycle=',b'ELDEN_RECOVERY PANIC_WAIT',b'ELDEN_UFS final completion recheck']:
            assert text in kernel,text
        reports.append({'variant':variant,'sha256':a.sha256,'session':packet['session'],'flags':packet['flags'],
            'index':a.index,'footer_location':a.location,'dsp_writes_enabled':adsp==2,'device_tested':False})
    report={'accepted':True,'images':reports,'kernel_sha256':sha(same),'original_fd_shim_and_assets_preserved':True,
        'first_test':'firmware','pmic_gate':'Requires successful firmware/survey, live reserved-memory/IRQ/power qualification','hardware_tested':False}
    (root/'power-mode-audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
