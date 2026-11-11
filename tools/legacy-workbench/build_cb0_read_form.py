"""Mac-only local image experiment: fixed-address reads of the active UFS CB0.

No emulator or device I/O. Retains original mapping/geometry checks, then samples
only CB0 if SMR0/S2CR0 still match the observed SID60 route. All results stop in panic.
"""
from pathlib import Path
import hashlib,json,struct,subprocess,sys,os

ROOT=Path('/Users/ciscohe/Desktop/UniXNU_elden')
sys.path.insert(0,str(ROOT/'scripts'))
import make_entry_probe_kernel as maker
os.chdir(ROOT)
out=ROOT/'out/fix-cb0-read-form-20261106'
out.mkdir(exist_ok=False)
env=os.environ.copy();env['PATH']=str(ROOT/'.cache/mtools/bin')+os.pathsep+env['PATH']
payload=out/'payload.img'
import shutil
shutil.copyfile(ROOT/'out/fix-arm-io-phandle-rootcheck-20261106/payload.img',payload)
subprocess.run(['mcopy','-o','-i',str(payload),'::EFI/UniXNU/Payloads/kernel.macho',str(out/'kernel-original.macho')],check=True,env=env)
original=(out/'kernel-original.macho').read_bytes()
assert hashlib.sha256(original).hexdigest()=='3fd40e9f88bbcfdde801d9d5bfed9a9a836921393ff5bfed2c1058137c73e4b6'
kernel=bytearray(original)
start=0xfffffff007968ab4
_,offset=maker.locate(original,start,4,executable=True)
_,fault=maker.locate(original,0xfffffff007968ad8,4,executable=True)
assert original[fault:fault+4]==bytes.fromhex('234640b8') # ldr w3,[x17],#4
needle=b'NATIVE_UFS cb survey\n'
assert original.count(needle)==1
fmt_offset=original.index(needle);fmt_end=original.index(b'\0',fmt_offset)+1
fmt_va=next(s['address']+fmt_offset-s['file_offset'] for s in maker.segments(original)
            if s['file_offset']<=fmt_offset<s['file_offset']+s['file_size'])
fmt=(b'NATIVE_UFS CB0 ONLY\nidr1=%08llx ncb=%llu pages=%llu\n'
     b'smr0=%08llx s2cr0=%08llx\ncbar0=%08llx cba2r0=%08llx sctlr0=%08llx\0')
assert len(fmt)<=fmt_end-fmt_offset
source=maker.STUB_HEADER+'''
  ldr w15, [x0, #0x800]
  ldr w16, [x0, #0xc00]
  movz w11, #0xdead
  movk w11, #0xdead, lsl #16
  mov w12, w11
  mov w14, w11
  mov w13, #0x60
  movk w13, #0x8000, lsl #16
  cmp w15, w13
  b.ne report
  cbnz w16, report
  ldr w11, [x0, #0x1000]
  ldr w12, [x0, #0x1800]
  add x13, x0, x9, lsl #12
  ldr w14, [x13]
report:
  stp x10, x8, [sp]
  stp x9, x15, [sp, #16]
  stp x16, x11, [sp, #32]
  stp x12, x14, [sp, #48]
'''
for i in range(4):
    source+=f'  {"movz" if i==0 else "movk"} x0, #{(fmt_va>>(16*i))&65535}, lsl #{16*i}\n'
source+='  .word 0x94000000\n  b .\n'
code=bytearray(maker.assemble(Path('/usr/bin/clang'),source))
panic_va=maker.symbols(original)['_panic']
call_va=start+len(code)-8
delta=(panic_va-call_va)//4
assert -(1<<25)<=delta<(1<<25)
struct.pack_into('<I',code,len(code)-8,0x94000000|(delta&0x3ffffff))
assert start+len(code)<0xfffffff007968ba0 # keep original geometry failure intact
kernel[offset:offset+len(code)]=code
kernel[fmt_offset:fmt_end]=fmt.ljust(fmt_end-fmt_offset,b'\0')
allowed=((offset,offset+len(code)),(fmt_offset,fmt_end))
assert len(kernel)==len(original) and all(a==b or any(lo<=i<hi for lo,hi in allowed) for i,(a,b) in enumerate(zip(original,kernel)))
(out/'kernel.macho').write_bytes(kernel);(out/'probe.S').write_text(source)
report={'kernel_sha256':hashlib.sha256(kernel).hexdigest(),'patch_va':hex(start),'patch_bytes':len(code),
        'patch_file_offset':offset,'format_va':hex(fmt_va),'format_file_range':[fmt_offset,fmt_end],
        'original_fault_instruction':'ldr w3,[x17],#4','reads':'SMR0/S2CR0 admission then CBAR0/CBA2R0/SCTLR0 only',
        'route_mismatch_values':'deaddead','panic_arguments':'8 uint64_t values on Darwin varargs stack',
        'panic_target':hex(panic_va),'geometry_error_intact':True,'no_mmio_writes':True}
(out/'patch.json').write_text(json.dumps(report,indent=2)+'\n')
subprocess.run(['mcopy','-o','-i',str(payload),str(out/'kernel.macho'),'::EFI/UniXNU/Payloads/kernel.macho'],check=True,env=env)
subprocess.run(['python3','scripts/build_phase2_image.py','--kind','boot','--fd','out/phase3/old-fd-verified.bin',
 '--payload',str(payload),'--output',str(out/'boot.img'),'--embed-payload','--work',str(out),'--clang','/usr/bin/clang'],check=True,env=env)
print(json.dumps(report,indent=2))
