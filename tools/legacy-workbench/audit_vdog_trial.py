"""Inspect the actual linked watchdog trial and the exact v77 source delta."""
from pathlib import Path
import argparse,hashlib,json,re,sys
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
parser=argparse.ArgumentParser();parser.add_argument('--version',type=int,choices=[78,79,80],default=78);v=parser.parse_args().version
r=Path(__file__).resolve().parents[1];p=r/f'edl_work/basic-bundle-20261108/v{v}'
src=r/f'research/basic-bundle-20261108/source-compiled-v{v}';old=r/'research/basic-bundle-20261108/source-compiled-v77'
sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts');import make_entry_probe_kernel as m
sha=lambda b:hashlib.sha256(b).hexdigest()
b=(p/'final/kernel.macho').read_bytes();s=m.symbols(b);addresses=sorted(set(s.values()));reverse={v:k for k,v in s.items()}
source=(src/'usb/EldenSdamRecovery.inc').read_text();before=(old/'usb/EldenSdamRecovery.inc').read_text()
start=source.index('\n/* ELDEN_VDOG_TRIAL_BEGIN */\n');end=source.index('/* ELDEN_VDOG_TRIAL_END */\n',start)+len('/* ELDEN_VDOG_TRIAL_END */\n')
restored=source[:start]+source[end:]
restored=restored.replace('\nstatic void eldenStartVdogTrial(void);','').replace('\n  eldenStartVdogTrial();','')
assert restored==before,'Unexpected change outside the explicitly added watchdog hook/body'
proof=json.loads((p/'vdog-source.json').read_text());assert sha((src/'usb/EldenSdamRecovery.inc').read_bytes())==proof['recovery_source_sha256']
for folder in ('power','backlight','xnu'):
 for f in (old/folder).rglob('*'):
  if f.is_file():assert (src/f.relative_to(old)).read_bytes()==f.read_bytes(),str(f)
for name in ('usb/EldenUsbDebugXnu.inc','usb/Dwc3.c','tree/scripts/driver-patches/EldenHidGlue.cpp','tree/scripts/driver-patches/EldenDisplayGlue.cpp'):
 assert (src/name).read_bytes()==(old/name).read_bytes(),name
functions=[n for n in s if (n.startswith('_elden_vdog_') or any(k in n for k in ('eldenVdog','EldenVdog','eldenStartVdogTrial'))) and s[n]>0xfffffff007000000]
bark_ms=27000 if v>=80 else 30000
allowed={(0x86000006,0,0,0),(0x86000008,bark_ms,30000,0),(0x86000005,3,0,0),(0x86000005,2,0,0)}
if v>=79:allowed.add((0x86000007,0,0,0))
records={};calls=set();decoded_smc=[];arm_operations=[]
for name in functions:
 a=s[name];end=next(x for x in addresses if x>a)
 try:off=m.locate(b,a,executable=True)[1]
 except ValueError:continue
 raw=b[off:off+end-a];decoder=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);decoder.detail=True;ins=list(decoder.disasm(raw,a));assert ins
 fp=[i for i in ins if re.search(r'\b[qvdshb]\d+(?:\b|\.)',i.op_str)];assert not fp,name
 callee={reverse.get(int(i.op_str[1:],16),'') for i in ins if i.mnemonic in ('b','bl') and i.op_str.startswith('#0x')};calls|=callee
 (p/(name+'.asm')).write_text('\n'.join(f'{i.address:x}: {i.mnemonic} {i.op_str}' for i in ins)+'\n')
 records[name]={'address':hex(a),'instructions':len(ins),'sha256':sha(raw),'fp_simd':False,'calls':sorted(callee-{''})}
 if name=='_elden_vdog_arm' and v>=79:
  values={}
  for i in ins:
   if i.mnemonic=='bl':
    target=reverse.get(int(i.op_str[1:],16),'')
    if target.startswith('__ZL6invokeP9EldenVdog'):
     assert all(n in values for n in (3,4,5)),(hex(i.address),values)
     arm_operations.append([values[n] for n in (3,4,5)])
    for n in range(19):values.pop(n,None)
    continue
   match=re.fullmatch(r'([wx])(\d+), #(-?0x[0-9a-f]+|-?\d+)(?:, lsl #(\d+))?',i.op_str)
   if i.mnemonic in ('mov','movk') and match:
    bits=32 if match[1]=='w' else 64;reg=int(match[2]);value=int(match[3],0);shift=int(match[4] or '0')
    if i.mnemonic=='mov':values[reg]=value&((1<<bits)-1)
    elif reg in values:values[reg]=((values[reg]&~(0xffff<<shift))|(value<<shift))&((1<<bits)-1)
    continue
   match=re.fullmatch(r'([wx])(\d+), [wx](\d+), #(0x[0-9a-f]+|\d+)',i.op_str)
   if i.mnemonic in ('add','sub') and match and int(match[3]) in values:
    value=int(match[4],0)*(1 if i.mnemonic=='add' else -1);bits=32 if match[1]=='w' else 64
    values[int(match[2])]=(values[int(match[3])]+value)&((1<<bits)-1);continue
   for register in i.regs_access()[1]:
    match=re.fullmatch(r'[wx](\d+)',i.reg_name(register))
    if match:values.pop(int(match[1]),None)
 if 'eldenVdogTrialWorker' in name:
  for index,i in enumerate(ins):
   if i.mnemonic!='smc':continue
   values={}
   for previous in ins[max(0,index-18):index]:
    if previous.mnemonic in ('smc','bl','blr'):values.clear();continue
    match=re.fullmatch(r'([wx])([0-3]), #(-?0x[0-9a-f]+|-?\d+)(?:, lsl #(\d+))?',previous.op_str)
    if previous.mnemonic in ('mov','movk') and match:
     bits=32 if match[1]=='w' else 64;reg=int(match[2]);value=int(match[3],0);shift=int(match[4] or '0')
     if previous.mnemonic=='mov':values[reg]=value&((1<<bits)-1)
     else:
      if reg in values:values[reg]=((values[reg]&~(0xffff<<shift))|(value<<shift))&((1<<bits)-1)
    else:
     for register in previous.regs_access()[1]:
      matched=re.fullmatch(r'[wx]([0-3])',previous.reg_name(register))
      if matched:values.pop(int(matched[1]),None)
   assert len(values)==4,(hex(i.address),values)
   args=tuple(values[n] for n in range(4))
   assert args in allowed,(hex(i.address),args)
   decoded_smc.append({'address':hex(i.address),'registers':list(args)})
assert '_elden_vdog_smc' in records and '_elden_vdog_arm' in records
assert any('eldenVdogTrialWorker' in n for n in records)
marker=(b'ELDEN_VDOG EXPECT_EXPIRATION seconds=30 no-pet=1' if v==78 else b'ELDEN_VDOG EXPECT_EXPIRATION seconds=30 initial-pet=1 periodic-pet=0' if v==79 else b'ELDEN_VDOG EXPECT_EXPIRATION seconds=30 bark-seconds=27 initial-pet=1 periodic-pet=0')
assert marker in b
# The qualification worker must not service the hardware timer while waiting.
worker=next(v for n,v in records.items() if 'eldenVdogTrialWorker' in n)
assert '_elden_vdog_pet' not in worker['calls']
if not {'_elden_vdog_arm','_elden_vdog_status','_elden_vdog_stop'}<=set(worker['calls']):
 if v>=79 and '_elden_vdog_arm' in worker['calls']:
  assert arm_operations==[[0x86000008,bark_ms,30000],[0x86000005,3,0],[0x86000007,0,0]]
  assert {tuple(x['registers']) for x in decoded_smc}=={(0x86000006,0,0,0),(0x86000005,2,0,0)}
 else:assert decoded_smc and {tuple(x['registers']) for x in decoded_smc}==allowed
if v>=79:
 assert source.count('if(!rc)rc=invoke(s,call,arg,ELDEN_VDOG_PET,0,0,&r);')==1
 assert source.count('ELDEN_VDOG REPLY n=')==1 and 'EldenVdogTrace trace[8]' in source
 runtime=source[source.index('static void eldenVdogTrialWorker('):]
 loop=runtime[runtime.index('while(eldenDeadlineCounter()-armed'):runtime.index('static void eldenStartVdogTrial(void)')]
 assert 'elden_vdog_pet' not in loop and 'ELDEN_VDOG_PET' not in loop
 if v>=80:assert 'if(rc||!(gEldenVdog.control&1))break;' in loop and '||gEldenVdog.expired' not in loop
assert '0x86000005' not in before and 'vdog' not in before
report={'accepted':True,'kernel_sha256':sha(b),'recovery_sha256':sha((src/'usb/EldenSdamRecovery.inc').read_bytes()),'functions':records,'inlined_smc':decoded_smc,'arm_operations':arm_operations,
        'original_controls_unchanged':True,'no_pet_trial':v==78,'initial_pet':int(v>=79),'periodic_pet':False,'seconds':30,'bark_ms':bark_ms,'smp_enabled':False,'normal_panic_code_unchanged':True,
        'limits':['Opt-in trial expiration can bound a panic while armed; not the production 180-second integration','Actual timer expiration and fastboot routing remain unqualified'],
        'source_proof_sha256':sha((p/'vdog-source.json').read_bytes()),'hardware_tested':False}
(p/'vdog-codegen.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'accepted':True,'kernel_sha256':report['kernel_sha256'],'functions':len(records)},indent=2))
