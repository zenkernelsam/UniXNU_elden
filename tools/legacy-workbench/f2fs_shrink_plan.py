"""Plan only: decode both F2FS superblocks and preserve the kernel's size remainder.

Requires a local dump of the first 8192 bytes of the decrypted filesystem and
the intended NEW capacity of that same dm mapping, in 4096-byte blocks. It
never opens a device, issues an ioctl, edits a superblock, or changes GPT.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

MAGIC=0xf2f52010
SUPER_BYTES=3072
CRC_OFFSET=3068


def plan_counts(old,capacity,section):
    if not all(isinstance(n,int) and n>0 for n in (old,capacity,section)):
        raise ValueError('positive integer block counts required')
    if capacity>=old or section>old:
        raise ValueError('this planner handles shrinking only')
    remainder=old%section
    if capacity<=remainder:
        raise ValueError('capacity cannot contain one surviving section')
    request=((capacity-remainder)//section)*section
    removed_sections=(old-request)//section
    final=old-removed_sections*section
    if request<=0 or removed_sections<=0 or final>capacity or removed_sections*section>0x7fffffff:
        raise ValueError('unsupported shrink or signed-32-bit block delta')
    assert request%section==0 and final%section==remainder
    return dict(old_blocks=old,new_mapping_capacity_blocks=capacity,section_blocks=section,
                request_blocks=request,predicted_final_blocks=final,
                retained_remainder_blocks=remainder,removed_sections=removed_sections,
                margin_blocks=capacity-final,naive_request_blocks=capacity//section*section,
                naive_result_blocks=old-((old-capacity//section*section)//section)*section)


def parse_superblocks(data):
    if len(data)!=8192:
        raise ValueError('exactly two 4096-byte blocks from decrypted filesystem required')
    first=data[1024:4096];second=data[5120:8192]
    if first!=second:
        raise ValueError('superblocks disagree; do not plan a resize')
    u32=lambda offset:struct.unpack_from('<I',first,offset)[0]
    u64=lambda offset:struct.unpack_from('<Q',first,offset)[0]
    if u32(0)!=MAGIC or u32(16)!=12 or u32(20)!=9 or u32(8)+u32(12)!=12 or not 9<=u32(8)<=12:
        raise ValueError('unsupported F2FS 4KiB geometry')
    sections,segs_per_sec=u32(44),u32(24)
    if not sections or not segs_per_sec or segs_per_sec>0x7fffffff//512:
        raise ValueError('invalid section geometry')
    if any(first[2201+68*i:2201+68*i+64].strip(b'\0') for i in range(8)):
        raise ValueError('explicit/multiple device layout needs separate review')
    feature=u32(2180)
    if feature&0x800:
        expected=(zlib.crc32(first[:CRC_OFFSET],MAGIC^0xffffffff)^0xffffffff)&0xffffffff
        if u32(32)!=CRC_OFFSET or u32(CRC_OFFSET)!=expected:
            raise ValueError('F2FS superblock checksum mismatch')
    old=u64(36);main=u32(92);main_segments=u32(68)
    if not main or not old or not main_segments or main_segments!=sections*segs_per_sec or main+main_segments*512>old:
        raise ValueError('main area does not fit filesystem or section count')
    metadata_segments=sum(u32(n) for n in (52,56,60,64))
    if u32(48)!=metadata_segments+main_segments:
        raise ValueError('segment counts disagree')
    return dict(old_blocks=old,section_blocks=512*segs_per_sec,sections=sections,
                main_block=main,main_segments=main_segments,feature=hex(feature),
                superblock_crc_present=bool(feature&0x800),
                superblock_sha256=hashlib.sha256(first).hexdigest())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--superblocks',type=Path,required=True)
    p.add_argument('--new-mapping-blocks',type=int,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();geometry=parse_superblocks(a.superblocks.read_bytes())
    result=plan_counts(geometry['old_blocks'],a.new_mapping_blocks,geometry['section_blocks'])
    if result['removed_sections']>=geometry['sections'] or result['predicted_final_blocks']<=geometry['main_block']:
        raise ValueError('requested shrink removes the entire main area')
    result.update(mode='OFFLINE PLAN ONLY',geometry=geometry,
        reviewed_kernel='6.6.118-android15-8-g350f378eb496-ab14943902-4k',
        kernel_Image_sha256='55d1056dd3d11e8cb5ab069a6ccb041731a5d03daadcca6d611fe1f8b3952b05',
        actual_resize_executed=False,ready_to_change_GPT=False,
        required=['fresh dm mapping offsets and reserved tail checked against proposed GPT',
                  'verified recoverable userdata ciphertext and metadata backup',
                  'free sections/checkpoint/fsck/mount state inspected',
                  'actual post-resize superblocks AND checkpoint checked before GPT changes'])
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
