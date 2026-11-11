"""Read-only M0 inventory of the exact accepted GUI artifact and its source evidence."""
import argparse,gzip,hashlib,json,struct,sys,tomllib
from pathlib import Path
from audit_boot_candidate import Fat
from avb_guard import check_image,metadata

ROOT=Path(__file__).resolve().parents[1]
PORT=ROOT.parents[1]/'UniXNU_elden'
sys.path.insert(0,str(PORT/'scripts'))
from darwinboot_preflight import parse_kernel,adt_walk

def sha(b):return hashlib.sha256(b).hexdigest()
def main():
    if sys.flags.optimize:raise SystemExit('Validation requires assertions enabled')
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    directory=ROOT/'edl_work/services-20261107';image=directory/'boot-signed.img'
    admitted=check_image(image,'boot_a')
    assert admitted.sha256=='a046134e11d065f25b10eb44cb3834ea97343502c6418097c100c1007b5ba520'
    assert admitted.index==1738713600 and admitted.location==0 and admitted.bytes==100663296
    assert metadata(image)['props']==metadata(ROOT/'signing/phase5_resign/boot_signed.img')['props']
    b=image.read_bytes();raw=gzip.decompress(b[4096:4096+struct.unpack_from('<I',b,8)[0]])
    assert len(raw)==128+4*1024*1024+56*1024*1024
    fat=Fat(raw[0x400080:]);packet=json.loads((directory/'packet.json').read_text())
    files={}
    paths=['Payloads/kernel.macho','Payloads/kernel.macho.adt','Payloads/kernel.macho.trustcache','Payloads/data.packed','EldenPayloadLoader.efi','unixnu.toml']
    for path in paths:
        data=fat.read('/EFI/UniXNU/'+path);files[path]={'bytes':len(data),'sha256':sha(data)}
    kernel=fat.read('/EFI/UniXNU/Payloads/kernel.macho')
    assert sha(kernel)==packet['kernel_sha256']==sha((directory/'kernel.macho').read_bytes())
    for path,digest in packet['unchanged'].items():assert files[path]['sha256']==digest
    menu=tomllib.loads(fat.read('/EFI/UniXNU/unixnu.toml').decode())
    assert menu['boot']['default']=='xnu' and menu['boot']['timeout']==8
    assert len({e['id'] for e in menu['entry']})==len(menu['entry'])
    assert all(len(e.get('args','').encode())<1024 for e in menu['entry'])
    assert 'elden-debug-session='+packet['session'] in menu['entry'][0]['args']
    assert adt_walk(fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt'))['window']==0x180000000
    parsed=parse_kernel(kernel)
    report=json.loads((directory/'build-report.json').read_text());source_records={}
    for path,digest in report['source_sha256'].items():
        if path.startswith('scripts/'):
            local=PORT/path;actual=sha(local.read_bytes())
            source_records[path]={'expected':digest,'actual':actual,'matches':actual==digest}
    # The report may cover only part of the build; it is not a complete rebuild proof.
    evidence={}
    for path in ['packet.json','build-report.json','derivative.json','qualification.json','boot-signed.signing.json']:
        source=directory/path
        if source.exists():evidence[path]=sha(source.read_bytes())
    result={'schema':1,'scope':'exact accepted artifact inventory; not a fresh-tree rebuild',
        'image':str(image),'image_sha256':admitted.sha256,'bytes':admitted.bytes,'stock_index':admitted.index,
        'footer_location':admitted.location,'stock_properties_equal':True,'shim_sha256':sha(raw[:128]),
        'fd_sha256':sha(raw[128:0x400080]),'payload_sha256':sha(raw[0x400080:]),'payload_bytes':56*1024*1024,
        'files':files,'session':packet['session'],'default':'xnu','timeout':8,'memory_bytes':6442450944,
        'kernel_entry':hex(parsed['entry']),'source_records':source_records,'evidence_hashes':evidence,
        'source_drift':any(not v['matches'] for v in source_records.values()),'device_access':False}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'image_sha256':admitted.sha256,'stock_index':admitted.index,'footer_location':admitted.location,
        'assets':len(files),'payload_bytes':result['payload_bytes'],'source_drift':result['source_drift'],'output':str(a.output)},indent=2))
if __name__=='__main__':main()
