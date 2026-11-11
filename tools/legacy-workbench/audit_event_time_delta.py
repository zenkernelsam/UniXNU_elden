"""Bind the v73 read-only clock observations to the prior v72 controls."""
from pathlib import Path
import json,hashlib
r=Path(__file__).resolve().parents[1];base=r/'research/basic-bundle-20261108/source-compiled-v72';new=r/'research/basic-bundle-20261108/source-compiled-v73'
assert new.is_dir()
records={};sha=lambda b:hashlib.sha256(b).hexdigest()
name='tree/scripts/driver-patches/EldenUserWaitProbe.c';b=(new/name).read_text();original=b
start=b.index('/* Exact 21H461 libsystem_kernel mach_absolute_time reads offset at')
end=b.index('static int capture(',start)
b=b[:start]+b[end:];b=b.replace('    userClock(map,name,proc_pid(p));\n','')
assert b==(base/name).read_text(),'Sampler controls changed'
records[name]=dict(old=sha(b.encode()),new=sha(original.encode()))
name='tree/build/sheng-hid-input/src/NativeHidInput.cpp';b=(new/name).read_text();original=b
b=b.replace('extern "C" void clock_get_uptime(uint64_t *);\n','')
start=b.index('    uint64_t eventTime=call<uint64_t>(event,16),now=0,rawTime=0;')
end=b.index('    char line[400];',start)
b=b[:start]+b[end:]
b=b.replace('    char line[400];','    char line[280];',1)
b=b.replace(' range=%d time=%llx now=%llx raw-time=%llx options=%x\\n',' range=%d\\n',1)
b=b.replace('0xB0008,0),eventTime,now,rawTime,(unsigned)timeOptions);','0xB0008,0));',1)
assert b==(base/name).read_text(),'Report/edge controls changed'
records[name]=dict(old=sha(b.encode()),new=sha(original.encode()))
out=r/'research/hid-event-time-20261111/v73-source-delta.json'
out.write_text(json.dumps(dict(accepted=True,compiled_snapshot=True,controls_unchanged=True,files=records,
    user_read_address='0xfffffc088',user_read_bytes=16,reads_per_sample=2,event_time_slot=16,hardware_tested=False),indent=2)+'\n')
print('Verified clock-only source delta; report/edge/sampler controls unchanged')
