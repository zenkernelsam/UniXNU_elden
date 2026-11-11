"""WSL: prepare complete rescue assets for the EDK LZMA encoder.

Preserves both vendor and TWRP CPIO archives, rebuilding only the bootconfig
trailer for the uncompressed concatenation. No hardware or filesystem mount.
"""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
from pathlib import Path
import struct


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--kernel',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True)
    lib=ctypes.CDLL(ctypes.util.find_library('lz4'))
    decode=lib.LZ4_decompress_safe
    decode.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
    decode.restype=ctypes.c_int

    def legacy(blob):
        assert blob[:4]==bytes.fromhex('02214c18')
        at=4
        result=bytearray()
        destination=ctypes.create_string_buffer(8*1024*1024)
        while at<len(blob):
            assert len(blob)-at>=4
            size=struct.unpack_from('<I',blob,at)[0];at+=4
            if not size:
                assert not any(blob[at:]);break
            assert size<=9*1024*1024 and at+size<=len(blob)
            compressed=ctypes.create_string_buffer(blob[at:at+size])
            count=decode(compressed,destination,size,len(destination))
            assert 0<count<=len(destination)
            result.extend(destination.raw[:count]);at+=size
            assert len(result)<=512*1024*1024
        assert result[:6] in (b'070701',b'070702')
        return bytes(result)

    info=json.loads((a.source/'inputs.json').read_text())
    linux=(a.source/'Image').read_bytes()
    initrd=(a.source/'recovery-initrd.img').read_bytes()
    assert sha(linux)==info['kernel']['sha256']=='55d1056dd3d11e8cb5ab069a6ccb041731a5d03daadcca6d611fe1f8b3952b05'
    assert sha(initrd)==info['initrd']['sha256']=='e4654999b23fee61656d8a0d315b0162765452e13a2c1ce7fab945c14566f92d'
    vsize,tsize=info['initrd']['vendor_bytes'],info['initrd']['twrp_bytes']
    vendor=legacy(initrd[:vsize])
    recovery=legacy(initrd[vsize:vsize+tsize])
    assert initrd[-12:]==b'#BOOTCONFIG\n'
    count,checksum=struct.unpack_from('<II',initrd,len(initrd)-20)
    config=initrd[-20-count:-20]
    assert len(initrd)-20-count==vsize+tsize and sum(config)&0xffffffff==checksum
    config=config.rstrip(b'\0')+b'\0'
    assert b'\0' not in config[:-1]
    raw=vendor+recovery
    config+=bytes((-len(raw)-len(config))%4)
    raw+=config+struct.pack('<II',len(config),sum(config)&0xffffffff)+b'#BOOTCONFIG\n'
    kernel=a.kernel.read_bytes()
    assert sha(kernel)=='3fd40e9f88bbcfdde801d9d5bfed9a9a836921393ff5bfed2c1058137c73e4b6'
    records={}
    for name,data in (('Image.efi',linux),('recovery-initrd.cpio',raw),('kernel.macho',kernel)):
        (a.output/name).write_bytes(data)
        records[name]=dict(bytes=len(data),sha256=sha(data))
        print(name,records[name],flush=True)
    report=dict(assets=records,
                vendor_cpio_bytes=len(vendor),twrp_cpio_bytes=len(recovery),
                raw_bootconfig_checksum_valid=True,all_cpio_content_preserved=True,
                compression_complete=False,EFI_decoder_executed=False,hardware_tested=False)
    (a.output/'raw-assets.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__':
    main()
