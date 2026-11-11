"""Mac preparation of an unchanged owned-kernel QEMU metadata fixture.

Uses actual source helper encodings. Does not claim UEFI handoff or UFS I/O.
"""
from pathlib import Path
import argparse
import importlib.util
import json
import shutil
import subprocess
import sys
import tomllib

root=Path.cwd()
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--candidate',type=Path,default=root/'out/bringup-20261105/kernel-gop.macho')
parser.add_argument('--audit',type=Path,default=root/'out/elden-storage-abi/build/storage-abi/static-audit.json')
parser.add_argument('--work',type=Path,default=root/'out/elden-storage-abi/qemu')
parser.add_argument('--command-line',default='-v debug=0x14e elden-gop-console=1 -enable_kprintf_spam security-mode-change-enable=1 amfi_get_out_of_my_way=1 -no_slto_panic')
parser.add_argument('--menu-config',type=Path,help='use the actual selected entry arguments from a candidate menu')
parser.add_argument('--entry',default='xnu')
args=parser.parse_args()
if args.menu_config:
    selected=[entry for entry in tomllib.loads(args.menu_config.read_text())['entry'] if entry['id']==args.entry]
    assert len(selected)==1 and selected[0]['type']=='xnu'
    args.command_line=selected[0]['args']
work=args.work.resolve()
work.mkdir(parents=True,exist_ok=True)
scripts=root/'third_party/UniXNU/scripts'
kernel=args.candidate.resolve()
dtcode=(scripts/'qemu-entry/make-dt.py').read_text()
dtcode=dtcode.replace('arm,cortex-a710','arm,armv8')
dtcode=dtcode.replace('str(Path(__file__).resolve().parents[1])',repr(str(scripts)))
(work/'make-qemu-a72-dt.py').write_text(dtcode)
subprocess.run([sys.executable,work/'make-qemu-a72-dt.py','--output',work/'qemu.adt',
    '--armvirt','--armcpu','--platform-compatible','xiaomi,sheng','--nvram-proxy',
    '--crypto-hash-method','sha2-384','--osenvironment','main','--amfi-platform-only',
    '--owned-memory-size','0x10000000','--dram-size','0x40000000'],check=True)
spec=importlib.util.spec_from_file_location('raw_entry',scripts/'probe-stock-entry.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
import struct
binary=kernel.read_bytes();position=32;virtuals=[]
for _ in range(struct.unpack_from('<I',binary,16)[0]):
    command,size=struct.unpack_from('<II',binary,position)
    if command==0x19 and struct.unpack_from('<Q',binary,position+32)[0]:
        virtuals.append(struct.unpack_from('<Q',binary,position+24)[0])
    position+=size
physical=0x50000000+(min(virtuals)&0x1fffff)
state=m.prepare(kernel,work,phys_base=physical,boot_args_pa=0x54400000,
    device_tree=work/'qemu.adt',command_line=args.command_line)
state['qemu_cpu']='max'
boot=bytearray((work/'boot-args.bin').read_bytes())
struct.pack_into('<Q',boot,0x18,0x10000000)
struct.pack_into('<Q',boot,0x478,0x10000000)
(work/'boot-args.bin').write_bytes(boot)
state['base_congruence_2m']=physical%0x200000==min(virtuals)%0x200000
(work/'entry.json').write_text(json.dumps(state,indent=2)+'\n')
for name in ('entry.S','link.ld','gdb-stop.py','gdb_packets.py','ufs_allocation.py','thread_snapshot.py'):
    shutil.copyfile(scripts/'qemu-entry'/name,work/name)
audit=json.loads(args.audit.read_text())
(work/'static-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
print(json.dumps({'state':state,'metaclass':audit['original_metaclass'],'source_metaclass':audit['source_ioservice_metaclass'],
                  'osmeta_ctor':audit['osmeta_ctor']},indent=2))
