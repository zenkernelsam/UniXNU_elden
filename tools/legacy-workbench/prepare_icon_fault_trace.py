from pathlib import Path
import hashlib,json,zipfile
r=Path(__file__).resolve().parents[1];d=Path('D:/Desktop/Git/UniXNU_elden/drivers/EldenFaultTrace');p=r/'research/icon-fault-trace-20261111';p.mkdir(exist_ok=True);assert not (p/'result.json').exists()
old=(r/'research/basic-bundle-20261108/source-compiled-v88/usb/EldenUsbDebugXnu.inc').read_bytes()
text=old.decode();anchor='extern "C" void elden_usb_user_fault(int pid,const char *name,uint32_t esr,uint64_t pc,';assert text.count(anchor)==1
text=text.replace(anchor,'#include "IconFaultTrace.inc"\n'+anchor)
anchor='  for(size_t i=0;i<(size_t)n && i<sizeof(line)-1;i++)elden_usb_log_putc(line[i]);\n}'
assert text.count(anchor)==1;text=text.replace(anchor,anchor[:-1]+'  eldenIconFaultTrace(pid,name,pc,lr);\n}')
files={n:(d/n).read_bytes() for n in ('FaultFrames.h','TestFaultFrames.c','IconFaultTrace.inc','IconFaultCapture.c')};files['EldenUsbDebugXnu.inc']=text.encode()
sha=lambda b:hashlib.sha256(b).hexdigest()
manifest={'parent_usb_sha256':sha(old),'files':{n:sha(b) for n,b in files.items()},'scope':'Only first four iconservicesagent PC0 faults; matching current EL0 state; bounded16 return addresses; read-only','hardware_tested':False}
for n,b in files.items():(p/n).write_bytes(b)
(p/'source.json').write_text(json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(p/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
 z.writestr('source.json',json.dumps(manifest,indent=2)+'\n')
