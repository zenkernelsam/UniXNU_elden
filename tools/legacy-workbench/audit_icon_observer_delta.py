"""Verify v71 only adds bounded observation; v70 controls remain byte-identical."""
from pathlib import Path
import hashlib,json,re
r=Path(__file__).resolve().parents[1];port=Path('D:/Desktop/Git/UniXNU_elden')
base=r/'research/basic-bundle-20261108/source-compiled-v70';new=r/'research/basic-bundle-20261108/source-compiled-v71'
sha=lambda b:hashlib.sha256(b).hexdigest();records={}
def read(name):
 p=new/name if new.exists() else port/name.removeprefix('tree/').replace('xnu/','patches/xnu/',1)
 return p.read_text()
name='tree/scripts/driver-patches/EldenHidGlue.cpp';text=read(name);current=text
text=text.replace('#include <kern/clock.h>\n','')
text=text.replace('    uint64_t downStart=0,downEnd=0,upStart=0,upEnd=0;\n','')
text=text.replace('clock_get_uptime(&downStart);down=elden_touch_hid_publish(&f);clock_get_uptime(&downEnd);IOSleep(100);','down=elden_touch_hid_publish(&f);IOSleep(100);')
text=text.replace('elden_nvt_frame empty={};clock_get_uptime(&upStart);up=elden_touch_hid_publish(&empty);clock_get_uptime(&upEnd);','elden_nvt_frame empty={};up=elden_touch_hid_publish(&empty);')
start=text.index('    if(downStart && downEnd>=downStart && upStart>=downEnd && upEnd>=upStart){')
end=text.index('\n    __atomic_store_n(&tapPending',start);text=text[:start]+text[end+1:]
assert text==(base/name).read_text(),'HID controls changed'
records[name]=dict(old=sha(text.encode()),new=sha(current.encode()))
name='tree/scripts/driver-patches/EldenUserWaitProbe.c';text=read(name);current=text
text=text.replace('extern int elden_icon_cpu_observe_process(proc_t) __attribute__((weak_import));\n','')
text=text.replace('        if(elden_icon_cpu_observe_process && elden_icon_cpu_observe_process(p))return 1;\n','')
text=text.replace('''        {
            struct sample *s=&samples[count++];
            if(state && is_saved_state64(state)) {
                s->pc=get_saved_state_pc(state);s->lr=get_saved_state_lr(state);s->fp=get_saved_state_fp(state);
                s->trap=get_saved_state_reg(state,16);
            } else {s->pc=0;s->lr=0;s->fp=0;s->trap=0;}
            s->state=(unsigned)thread->state;''','''        if(state && is_saved_state64(state)) {
            struct sample *s=&samples[count++];
            s->pc=get_saved_state_pc(state);s->lr=get_saved_state_lr(state);s->fp=get_saved_state_fp(state);
            s->trap=get_saved_state_reg(state,16);s->state=(unsigned)thread->state;''')
assert text==(base/name).read_text(),'Original sampler controls changed'
records[name]=dict(old=sha(text.encode()),new=sha(current.encode()))
name='xnu/EldenIconCpuCommon.inc';text=read(name);current=text
start=text.index('int elden_icon_cpu_observe_process(');end=text.index('static int\nelden_icon_cpu_selected(',start)
text=text[:start]+text[end:];assert text==(base/name).read_text(),'Owner policy changed'
records[name]=dict(old=sha(text.encode()),new=sha(current.encode()))
if new.exists():
 name='xnu/kern_exec.c';text=read(name);current=text
 text,count=re.subn(r'#if defined\(XNU_OWNER_DATA_MOUNT\) && XNU_OWNER_DATA_MOUNT\n\telden_icon_cpu_progress\([^\n]+\);\n#endif\n','',text)
 for anchor in ['\tproc_csflags_update(p, imgp->ip_csflags);\n\tproc_unlock(p);',
                '\tkr = task_get_task_access_port(proc_task(p), &port);',
                '\tkr = __EXEC_WAITING_ON_TASKGATED_CODE_SIGNATURE_UPCALL__(port, proc_getpid(p));',
                '\t\terror = proc_process_signature(p, &signature_failure_reason);',
                '\t\tkr = thread_process_signature(imgp->ip_new_thread, proc_get_task_raw(p));']:
  assert text.count(anchor+'\n\n')>=1;pos=text.index('static int\nprocess_signature(proc_t p, struct image_params *imgp)');text=text[:pos]+text[pos:].replace(anchor+'\n\n',anchor+'\n',1)
 assert count==11 and text==(base/name).read_text(),('Original exec changed',count)
 records[name]=dict(old=sha(text.encode()),new=sha(current.encode()))
out=r/'research/icon-launch-constraint-20261111/v71-observer-delta.json'
out.write_text(json.dumps(dict(accepted=True,compiled_snapshot=new.exists(),files=records,controls_unchanged=True),indent=2)+'\n')
print('Bounded exec/target-wait/tap-timing observation only; controls unchanged')
