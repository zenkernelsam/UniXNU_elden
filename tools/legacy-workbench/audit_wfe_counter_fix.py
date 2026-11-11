"""Offline: compare a WFE experiment to an admitted reset-checkpoint control."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
import sys
from audit_boot_candidate import Fat
from avb_guard import check_image
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parents[1]/'UniXNU_elden/scripts'))
from make_entry_probe_kernel import locate

def unpack(p):
    b=p.read_bytes()
    return gzip.decompress(b[4096:4096+struct.unpack_from('<I',b,8)[0]])

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('image',type=Path)
    ap.add_argument('control',type=Path)
    ap.add_argument('kernel_sha256')
    a=ap.parse_args()
    checked=check_image(a.image,'boot_a'); control=check_image(a.control,'boot_a')
    r,n=unpack(a.control),unpack(a.image)
    assert len(r)==len(n)==0x3c00080 and r[:0x400080]==n[:0x400080]
    f,g=Fat(r[0x400080:]),Fat(n[0x400080:]);kp='/EFI/UniXNU/Payloads/kernel.macho'
    old,new=f.read(kp),g.read(kp)
    assert hashlib.sha256(new).hexdigest()==a.kernel_sha256
    _,o=locate(new,0xfffffff0073f2f50,4,executable=True)
    assert old[:o]==new[:o] and old[o+4:]==new[o+4:]
    assert struct.unpack_from('<I',old,o)[0]==0xf9402529 and struct.unpack_from('<I',new,o)[0]==0xd53be009
    for path in (kp+'.adt','/EFI/UniXNU/unixnu.toml'):assert f.read(path)==g.read(path)
    ins=next(Cs(CS_ARCH_ARM64,CS_MODE_ARM).disasm(new[o:o+4],0xfffffff0073f2f50))
    assert (ins.mnemonic,ins.op_str)==('mrs','x9, cntfrq_el0')
    report=vars(checked)|{'path':str(checked.path),'control_sha256':control.sha256,
        'only_change_from_control':'0xfffffff0073f2f50: LDR metadata -> MRS CNTFRQ_EL0','device_written_by_this_audit':False}
    a.image.with_name('admission.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
