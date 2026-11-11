"""Independently inspect signed touch stages, original ABI and unchanged assets."""
from pathlib import Path
import gzip,hashlib,json,struct,sys,tomllib
from audit_boot_candidate import Fat,PORT
from avb_guard import check_image,metadata
sys.path.insert(0,str(PORT/'scripts'))
import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel,adt_walk
ROOT=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def unpack(path):
    b=path.read_bytes();return gzip.decompress(b[4096:4096+struct.unpack_from('<I',b,8)[0]])
def main():
    if sys.flags.optimize:raise SystemExit('Assertions must remain enabled')
    out=ROOT/'edl_work/touch-runtime-20261108';old=unpack(ROOT/'edl_work/hid-20261107/boot-signed.img');before=Fat(old[0x400080:])
    compiled=(out/'kernel.macho').read_bytes();symbols=maker.symbols(compiled);parse_kernel(compiled)
    plan=json.loads((out/'kernel.json').read_text());abi=json.loads((out/'hid-abi.json').read_text())
    assert abi['original_slots']==207 and abi['original_instance_size']==208
    assert abi['source_revision']=='5f3a37cec0c3fc4234b10726b25489a55f0d77ef'
    for name,va in abi['bindings'].items():assert symbols[name]==va,(name,symbols.get(name),va)
    # Retain the previously accepted single AMFI diagnostic branch. Reconstruct
    # that original word only in a local hash buffer; the artifact is not edited.
    original_bytes=bytearray(compiled);amfi=maker.locate(compiled,0xfffffff004aaecfc)[1]
    assert struct.unpack_from('<I',compiled,amfi)[0]==0x17ffffd4
    struct.pack_into('<I',original_bytes,amfi,0xaa1503e0)
    original={}
    for module,digest in plan['original_executable_sha256'].items():
        ext=next(e for e in plan['extents'] if e['module']==module and e['segment']=='__TEXT_EXEC')
        off=maker.locate(compiled,ext['va'])[1];assert sha(original_bytes[off:off+ext['size']])==digest;original[module]=digest
    patch=json.loads((out/'touch-board-patch.json').read_text())
    assert patch['original_event_driver_slots']==220 and patch['original_event_slots']==37
    assert patch['normalized_coordinates'] and patch['private_vtable_runtime_identity_gate']
    fw=json.loads((out/'touch-firmware-input.json').read_text());assert fw['bytes']==253952
    assert fw['sha256']=='413216d0e8738078414fa681262439ab6d1c181f02974215e0394f102fcc08dc'
    firmware=(ROOT/'research/twrp-device/recovery/root/vendor/firmware/novatek_ts_csot_3k_fw.bin').read_bytes()
    assert compiled.count(firmware)==1
    assert sha((ROOT/'research/nt36536-runtime-20261108/hid-owned-slots.json').read_bytes())=='24e14be9821b8c08040c2157aab80c42b189d5cc9fdcbbd3942833320bff8538'
    results=[]
    for variant,(mode,hid) in {'off':(0,0),'digitizer':(0,1),'identify':(1,0),'firmware':(2,0),'touch':(3,1)}.items():
        work=out/variant;admit=check_image(work/'boot-signed.img','boot_a');packet=json.loads((work/'packet.json').read_text())
        assert admit.index==1738713600 and admit.location==0 and admit.bytes==100663296
        assert metadata(work/'boot-signed.img')['props']==metadata(ROOT/'signing/phase5_resign/boot_signed.img')['props']
        raw=unpack(work/'boot-signed.img');assert raw[:0x400080]==old[:0x400080]
        fat=Fat(raw[0x400080:]);assert fat.read('/EFI/UniXNU/Payloads/kernel.macho')==compiled
        for path,digest in packet['unchanged'].items():
            blob=fat.read('/EFI/UniXNU/'+path);assert blob==before.read('/EFI/UniXNU/'+path) and sha(blob)==digest
        cfg=tomllib.loads(fat.read('/EFI/UniXNU/unixnu.toml').decode());assert cfg['boot']['default']=='xnu' and cfg['boot']['timeout']==8
        args=cfg['entry'][0]['args'].split();flags={}
        for word in args:
            if '=' in word:
                k,v=word.split('=',1);assert k not in flags;flags[k]=v
        assert int(flags['elden-touch'])==mode and int(flags['elden-touch-hid'])==hid and flags['elden-hid']=='1'
        assert flags['elden-debug-session']==packet['session'] and 'sheng-touch-hid' not in flags
        assert all(len(e.get('args','').encode())<1024 for e in cfg['entry'])
        assert adt_walk(fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt'))['window']==0x180000000
        results.append({'variant':variant,'sha256':admit.sha256,'session':packet['session'],'flags':packet['flags'],
                        'index':admit.index,'footer_location':admit.location,'hardware_tested':False})
    result={'accepted':True,'images':results,'kernel_sha256':sha(compiled),'original_executable_modules_verified':len(original),
            'hid_slots':207,'original_native_bindings':len(abi['bindings']),'event_driver_slots':220,'event_slots':37,
            'original_assets_preserved':True,'memory_bytes':6442450944,
            'accepted_original_instruction_patch':{'address':'0xfffffff004aaecfc','word':'0x17ffffd4'},
            'hardware_tested':False}
    (out/'touch-mode-audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
