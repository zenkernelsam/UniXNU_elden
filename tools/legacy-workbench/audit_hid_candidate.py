"""Independent signed FAT/kernel/source-ABI checks for the M1 HID candidate."""
import argparse,gzip,hashlib,json,struct,sys,tomllib
from pathlib import Path
from avb_guard import check_image,metadata
from audit_boot_candidate import Fat,PORT
sys.path.insert(0,str(PORT/'scripts'))
import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel,adt_walk
ROOT=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def unpack(path):
    b=path.read_bytes();return gzip.decompress(b[4096:4096+struct.unpack_from('<I',b,8)[0]])
def main():
    if sys.flags.optimize:raise SystemExit('Do not disable assertions')
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--directory',type=Path,required=True);a=p.parse_args()
    out=a.directory;image=out/'boot-signed.img';admitted=check_image(image,'boot_a')
    assert admitted.index==1738713600 and admitted.location==0 and admitted.bytes==100663296
    base=ROOT/'edl_work/services-20261107/boot-signed.img'
    assert metadata(image)['props']==metadata(base)['props']
    raw=unpack(image);original=unpack(base);assert raw[:0x400080]==original[:0x400080]
    fat=Fat(raw[0x400080:]);before=Fat(original[0x400080:]);packet=json.loads((out/'packet.json').read_text())
    kernel=fat.read('/EFI/UniXNU/Payloads/kernel.macho');adt=fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt')
    assert sha(kernel)==packet['kernel_sha256']==sha((out/'kernel.macho').read_bytes())
    for path,digest in packet['unchanged'].items():
        data=fat.read('/EFI/UniXNU/'+path);assert data==before.read('/EFI/UniXNU/'+path) and sha(data)==digest
    assert adt_walk(adt)['window']==0x180000000;parse_kernel(kernel)
    cfg=tomllib.loads(fat.read('/EFI/UniXNU/unixnu.toml').decode());assert cfg['boot']['timeout']==8 and cfg['boot']['default']=='xnu'
    assert 'elden-hid=1' in cfg['entry'][0]['args'] and 'elden-debug-session='+packet['session'] in cfg['entry'][0]['args']
    assert 'sheng-touch-hid=1' not in cfg['entry'][0]['args']
    assert all(len(e.get('args','').encode())<1024 for e in cfg['entry'])
    symbols=maker.symbols(kernel)
    for name in ['_elden_hid_home_ready','_elden_hid_request_home','_elden_preview_after_input','_native_hid_input_allocate']:
        assert name in symbols,name
    abi=json.loads((out/'hid-abi.json').read_text());assert abi['original_slots']==207 and abi['original_instance_size']==208
    assert abi['source_revision']=='5f3a37cec0c3fc4234b10726b25489a55f0d77ef'
    for name,target in abi['bindings'].items():assert symbols[name]==target,(name,symbols.get(name),target)
    plan=json.loads((out/'source-plan.json').read_text());hashes={}
    for module in ['com.apple.iokit.IOHIDFamily','com.apple.iokit.IOMobileGraphicsFamily']:
        extent=next(e for e in plan['extents'] if e['module']==module and e['segment']=='__TEXT_EXEC')
        at=maker.locate(kernel,extent['va'])[1];digest=sha(kernel[at:at+extent['size']])
        assert digest==plan['original_executable_sha256'][module];hashes[module]=digest
    result={'image_sha256':admitted.sha256,'index':admitted.index,'footer_location':admitted.location,
        'session':packet['session'],'assets_preserved':True,'original_family_hashes':hashes,
        'hid_slots':207,'native_bindings_verified':len(abi['bindings']),'memory_bytes':6442450944,
        'scope':'virtual keyboard/consumer HID; physical NT36536 touch not yet integrated','device_tested':False}
    (out/'independent-audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
