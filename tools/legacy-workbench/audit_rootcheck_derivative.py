"""Verify the signed derivative preserves the VM-qualified storage kernel."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
import sys

from audit_boot_candidate import Fat, PORT
from avb_guard import check_image

sys.path.insert(0, str(PORT/'scripts'))
import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image',type=Path,required=True)
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--abi',type=Path,required=True)
    p.add_argument('--menu',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    admitted=check_image(a.image,'boot_a')
    image=a.image.read_bytes()
    raw=gzip.decompress(image[4096:4096+struct.unpack_from('<I',image,8)[0]])
    fat=Fat(raw[0x400080:])
    candidate=fat.read('/EFI/UniXNU/Payloads/kernel.macho')
    menu=fat.read('/EFI/UniXNU/unixnu.toml')
    assert menu.replace(b'\r\n',b'\n')==a.menu.read_bytes().replace(b'\r\n',b'\n')
    baseline=a.baseline.read_bytes()
    audit=json.loads(a.abi.read_text(encoding='utf-8'))
    baseline_sha=hashlib.sha256(baseline).hexdigest()
    assert baseline_sha==audit['candidate_sha256']=='07d4df1d49e0dd64acab02e84a74d452c7034acd92e5a44b0ce37d67f53e4b15'
    assert audit['live_native_4k'] and audit['method_slots']==190
    assert audit['storage_lifecycle_observation']['function_return']==1
    old=parse_kernel(baseline)
    new=parse_kernel(candidate)
    assert old['entry']==new['entry'] and old['base']==new['base']
    old_segments=maker.segments(baseline)
    new_segments=maker.segments(candidate)
    assert new_segments[:-1]==old_segments
    stub=new_segments[-1]
    assert stub['name']=='__ELDEN_PROBE' and stub['file_size']==104
    assert stub['address']==(old['end']+16383)&~16383
    assert stub['file_offset']==(len(baseline)+16383)&~16383
    assert stub['size']==16384 and stub['protection']==5
    assert len(candidate)==stub['file_offset']+104
    assert candidate[len(baseline):stub['file_offset']]==bytes(stub['file_offset']-len(baseline))
    ncmds,cmdbytes=struct.unpack_from('<II',baseline,16)
    assert struct.unpack_from('<II',candidate,16)==(ncmds+1,cmdbytes+72)
    assert baseline[32+cmdbytes:32+cmdbytes+72]==bytes(72)
    entry=old['entry_file_offset']
    original_target=maker.branch_target(struct.unpack_from('<I',baseline,entry)[0],old['entry'])
    assert maker.branch_target(struct.unpack_from('<I',candidate,entry)[0],old['entry'])==stub['address']
    tail=struct.unpack_from('<I',candidate,stub['file_offset']+100)[0]
    assert maker.branch_target(tail,stub['address']+100)==original_target
    amfi=maker.locate(baseline,0xfffffff004aaecfc)[1]
    assert struct.unpack_from('<I',baseline,amfi)[0]==0xaa1503e0
    assert struct.unpack_from('<I',candidate,amfi)[0]==0x17ffffd4
    allowed=[(16,24),(32+cmdbytes,32+cmdbytes+72),(entry,entry+4),(amfi,amfi+4)]
    changed=[i for i,(x,y) in enumerate(zip(baseline,candidate)) if x!=y]
    assert all(any(start<=i<end for start,end in allowed) for i in changed)
    report=dict(image_sha256=admitted.sha256,baseline_sha256=baseline_sha,
        derivative_sha256=hashlib.sha256(candidate).hexdigest(),
        changed_existing_bytes=len(changed),allowed_existing_ranges=allowed,
        appended_padding_bytes=stub['file_offset']-len(baseline),entry_stub_bytes=104,
        storage_code_and_data_unchanged=True,menu_matches_source=True,
        baseline_live_storage_registration_passed=True,hardware_tested=False)
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
