"""WSL: rebuild a frozen dual11 control, then pack the48MiB rescue entry.

Reuses the exact known dual11 runtime. Never rebuilds or modifies /root/kshimc.
No device commands are issued. The result needs independent AVB re-signing.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'edl_work/prerequisites-20261106/kshim48'
PACKER=Path('/root/mbkp/output/MultiBootKernelPatcher')
STOCK=ROOT/'edl_work/rescue-compressed-20261106/Image.efi'
def sha(data):return hashlib.sha256(data).hexdigest()


def parse(kernel):
    candidates=[];at=0
    while True:
        at=kernel.find(b'KPMF',at)
        if at<0:break
        if at+60<=len(kernel):
            _,major,minor,count,default,timeout=struct.unpack_from('<IHHIII',kernel,at)
            if (major,minor)==(0,1) and 0<count<=50 and default<count:
                cursor=at+60;rows=[]
                for _ in range(count):
                    if cursor+56>len(kernel):break
                    values=struct.unpack_from('<QQIIQIIIQI',kernel,cursor);length=values[-1]
                    if length>256 or cursor+56+length>len(kernel):break
                    name=kernel[cursor+56:cursor+56+length].split(b'\0')[0].decode()
                    rows.append(dict(offset=values[0],bytes=values[1],type=values[2],flags=values[3],load=values[4],entry=values[5],align=values[7],copy_max=values[8],name=name))
                    cursor=(cursor+56+length+3)&~3
                if len(rows)==count:candidates.append(dict(manifest_offset=at,default=default,timeout=timeout,base=struct.unpack_from('<5Q',kernel,at+20),entries=rows))
        at+=4
    if len(candidates)!=1:raise ValueError('ambiguous packed manifest')
    return candidates[0]


def build(label,payload,copy_max,shim):
    directory=OUT/label;directory.mkdir(parents=True,exist_ok=True);packed=directory/'PackedKernel'
    if packed.exists():raise ValueError('output exists; inspect instead of replacing')
    config=f'''[Pack]
Shim={shim}
Output={packed}
Default=Image-00
Timeout=10000

[Image-00]
Name=原厂 Android
Path={STOCK}
Type=Linux
BaseImage=true
Align=21

[Image-UNIXNU]
Name=UniXNU elden
Path={payload}
Type=FreeExec
BaseImage=false
CopyTo=0xC2400000
CopySizeMax=0x{copy_max:x}
EntryOffset=0
Align=21

[Manifest]
Type=Manifest
'''
    cfg=directory/'menu.cfg';cfg.write_text(config)
    result=subprocess.run([PACKER,cfg],text=True,capture_output=True)
    (directory/'packer.log').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return packed


def main():
    signed=(ROOT/'edl_work/boot_dual11_signed.img').read_bytes()
    if sha(signed)!='e660fd44fd5ed81d8592cfa5c81ee1f3a147bf43bd91825b65549cdebbb9bbfb':raise ValueError('dual11 source differs')
    size=struct.unpack_from('<I',signed,8)[0];original=signed[4096:4096+size];metadata=parse(original)
    shim_entry=next(e for e in metadata['entries'] if e['type']==3)
    payload_entry=next(e for e in metadata['entries'] if e['type']==2)
    shim=original[shim_entry['offset']:shim_entry['offset']+shim_entry['bytes']]
    if sha(shim)!='8c1e18d419a50969fcf718e3585494ddba28cfe82a8a956f22ff666dafe66bf3':raise ValueError('safe runtime differs')
    if sha(STOCK.read_bytes())!='55d1056dd3d11e8cb5ab069a6ccb041731a5d03daadcca6d611fe1f8b3952b05':raise ValueError('stock kernel differs')
    OUT.mkdir(parents=True,exist_ok=True);runtime=OUT/'kShimC-dual11.bin';runtime.write_bytes(shim)
    old=OUT/'unixnu-dual11.bin';old.write_bytes(original[payload_entry['offset']:payload_entry['offset']+payload_entry['bytes']])
    control=build('control',old,0x1000000,runtime)
    if control.read_bytes()!=original:raise ValueError('control rebuild is not byte-identical; do not create a new dual image')
    new_payload=OUT/'unixnu-rescue48.bin';new=build('rescue48',new_payload,0x4000000,runtime)
    data=new.read_bytes();manifest=parse(data)
    if manifest['default']!=0 or manifest['timeout']!=10000:raise ValueError('Android default changed')
    entry=next(e for e in manifest['entries'] if e['type']==2)
    attached=next(e for e in manifest['entries'] if e['type']==3)
    if data[attached['offset']:attached['offset']+attached['bytes']]!=shim:raise ValueError('runtime changed')
    if data[entry['offset']:entry['offset']+entry['bytes']]!=new_payload.read_bytes():raise ValueError('payload truncated or changed')
    if entry['bytes']>entry['copy_max'] or entry['load']!=0xc2400000 or entry['entry']!=0:raise ValueError('invalid copy handoff')
    if len(data)+4096+65536>96*1024*1024:raise ValueError('packed kernel exceeds boot capacity')
    result=subprocess.run(['python3','/root/dualboot/repack_boot_v4.py','--original',ROOT/'signing/phase5_resign/boot_signed.img',
        '--kernel',new,'--output',OUT/'boot.img','--report',OUT/'boot-layout.json','--avbtool','/root/dualboot/avbtool.py'],text=True,capture_output=True)
    (OUT/'repack.log').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    report=dict(control_byte_identical=True,source_dual11_sha256=sha(signed),runtime_sha256=sha(shim),
        packer_sha256=sha(PACKER.read_bytes()),packed_bytes=len(data),packed_sha256=sha(data),manifest=manifest,
        payload_sha256=sha(new_payload.read_bytes()),hardware_tested=False,requires_resigning=True)
    (OUT/'pack-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
