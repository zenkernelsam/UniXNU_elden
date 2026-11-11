#!/usr/bin/env python3
"""Independent static FAT/entry/menu/AVB audit of a newly assembled candidate."""
from pathlib import Path
import argparse,gzip,hashlib,json,struct,sys,tomllib
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'tools/device'),str(ROOT/'scripts')]
from avb_guard import check_image,metadata
from boot_formats import Fat
from darwinboot_preflight import parse_kernel,adt_walk
import make_entry_probe_kernel as maker
from verify_v91_assets import verify_loader,verify_packed_seed
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
    if sys.flags.optimize:raise SystemExit('Run without -O; validation assertions must remain enabled')
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('image','entry-kernel','baseline-kernel','stock-boot','parent-vbmeta','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--data-report',type=Path)
    a=p.parse_args();checked=check_image(a.image,'boot_a');stock=check_image(a.stock_boot,'boot_a');check_image(a.parent_vbmeta,'vbmeta')
    if stock.sha256!='8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890':raise RuntimeError('Unknown stock contract')
    if metadata(a.image)['props']!=metadata(a.stock_boot)['props']:raise RuntimeError('AVB properties changed')
    b=a.image.read_bytes()
    if b[:8]!=b'ANDROID!' or struct.unpack_from('<I',b,40)[0]!=4 or struct.unpack_from('<I',b,12)[0]!=0:raise RuntimeError('Unexpected Android header')
    spec=json.loads((ROOT/'profiles/v91/boot-build/android-header.json').read_text());header=bytearray(4096);header[:8]=b'ANDROID!'
    struct.pack_into('<I',header,8,struct.unpack_from('<I',b,8)[0])
    for offset,key in [(12,'ramdisk_size'),(16,'os_version'),(20,'header_size'),(40,'header_version'),(1580,'signature_size')]:struct.pack_into('<I',header,offset,spec[key])
    header[44:44+len(spec['cmdline'].encode())]=spec['cmdline'].encode()
    if b[:4096]!=header:raise RuntimeError('Android header differs outside the variable kernel length')
    raw=gzip.decompress(b[4096:4096+struct.unpack_from('<I',b,8)[0]])
    if len(raw)!=128+4194304+58720256 or sha(raw[:128])!='ac27ecc2bcec5440606d77e69b7726921b0bf3803dd2234f23b10d1619149e1d' or sha(raw[128:0x400080])!='e50e1d6e2be51367225d76d4e97b90d51ba7812193d99fd4d4d7dd9fdc2ee534':raise RuntimeError('Shim/firmware mismatch')
    fat=Fat(raw[0x400080:]);sector=fat.data
    if fat.bits!=16 or sector[510:512]!=b'\x55\xaa' or fat.fats!=2:raise RuntimeError('Unexpected FAT layout')
    start=fat.reserved*fat.bps;length=fat.fat_sectors*fat.bps
    if sector[start:start+length]!=sector[start+length:start+2*length]:raise RuntimeError('FAT mirrors differ')
    prefix='EFI/UniXNU/';kernel=fat.read(prefix+'Payloads/kernel.macho')
    if kernel!=a.entry_kernel.read_bytes():raise RuntimeError('Kernel bytes in FAT differ')
    old=a.baseline_kernel.read_bytes()
    if sha(old)!='2d72dea2e1b69adf063a9db2b91b06cb16e1bc880425a868f62f6611989efe69':raise RuntimeError('Unknown hardware baseline kernel')
    def stub(image):
        s=next(x for x in maker.segments(image) if x['name']=='__ELDEN_PROBE');return s,image[s['file_offset']:s['file_offset']+s['file_size']]
    now,code=stub(kernel);prior,oldcode=stub(old)
    if len(code)!=104 or code[:-4]!=oldcode[:-4] or now['protection']!=5:raise RuntimeError('Entry body differs from the hardware-tested stub')
    old_return=maker.branch_target(struct.unpack_from('<I',oldcode,100)[0],prior['address']+100)
    new_return=maker.branch_target(struct.unpack_from('<I',code,100)[0],now['address']+100)
    if new_return!=old_return:raise RuntimeError('Relocated entry branch has a different return target')
    parsed=parse_kernel(kernel);word=struct.unpack_from('<I',kernel,parsed['entry_file_offset'])[0]
    if maker.branch_target(word,parsed['entry'])!=now['address']:raise RuntimeError('Entry branch misses stub')
    if struct.unpack_from('<I',kernel,maker.locate(kernel,0xfffffff004aaecfc)[1])[0]!=0x17ffffd4:raise RuntimeError('AMFI transform differs')
    expected={'Payloads/kernel.macho.adt':'169ea871e483d8ca989f6725c79f28698ebda937eeb9ca4514d85cec0354a9e3','Payloads/kernel.macho.trustcache':'583b2012d14c45f74532bccc0ec6eaf35143ba47b4dfbfabbe7dff5ced7ee67e','Payloads/data.packed':'483125a27280f17d91de594f951cee4216f4888b764525b74db0f61ed62d34b3','EldenPayloadLoader.efi':'ae8e5938b22766b921f43f68e2ea63db028fb57ec1f3791edaf7db10f5825299'}
    for name,h in expected.items():
        content=fat.read(prefix+name)
        if name=='EldenPayloadLoader.efi':verify_loader(content)
        elif name=='Payloads/data.packed' and sha(content)!=h:
            if not a.data_report:raise RuntimeError('Rebuilt Data seed verification is missing')
            verify_packed_seed(content,a.data_report)
        elif sha(content)!=h:raise RuntimeError('Frozen payload changed: '+name)
    tree=adt_walk(fat.read(prefix+'Payloads/kernel.macho.adt'))
    if tree['window']!=0x180000000 or tree['gpuaddon'] is not None:raise RuntimeError('Memory/GPU configuration changed')
    text=fat.read(prefix+'unixnu.toml').decode();cfg=tomllib.loads(text);template=(ROOT/'profiles/v91/boot-build/unixnu.toml.in').read_text()
    import re
    sessions=re.findall(r'elden-debug-session=([0-9a-f]{16})',text)
    if len(set(sessions))!=1 or text!=template.replace('@SESSION@',sessions[0]) or cfg['boot']['timeout']!=8 or cfg['boot']['default']!='xnu':raise RuntimeError('Menu or session policy differs')
    identity=kernel+template.encode()+fat.read(prefix+'Payloads/kernel.macho.adt')+hashlib.sha256(fat.read(prefix+'Payloads/kernel.macho.trustcache')).digest()
    for content in (raw[128:0x400080],fat.read(prefix+'EldenPayloadLoader.efi'),fat.read(prefix+'Payloads/data.packed')):identity+=hashlib.sha256(content).digest()
    if sessions[0]!=hashlib.sha256(identity).hexdigest()[:16]:raise RuntimeError('Debug session is not bound to all payload inputs')
    report={'accepted_static':True,'image_sha256':checked.sha256,'kernel_sha256':sha(kernel),'rollback_index':checked.index,'footer_location':checked.location,'parent_boot_chain_location':3,'fat_mirrors_equal':True,'entry_body_100_bytes_equal_hardware_baseline':True,'entry_branch_target_equal':True,'entry_address':hex(now['address']),'entry_return_target':hex(new_return),'session':sessions[0],'memory_window_bytes':tree['window'],'hardware_tested':False,'device_operation':False,'full_asset_source_closure':False}
    if a.output.exists():raise RuntimeError('Audit output must be new')
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
