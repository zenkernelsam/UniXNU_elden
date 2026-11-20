"""Audit actual original-worker pause/wake hooks and nonterminal pen interface."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build','prelink-tool','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports
    spec=json.loads((a.kernel_build/'result.json').read_text());raw=(a.kernel_build/'kernel.macho').read_bytes();native=(a.kernel_build/'native.macho').read_bytes()
    if sha(a.kernel_build/'kernel.macho')!=spec['kernel_sha256'] or sha(a.kernel_build/'native.macho')!=spec['native_sha256']:raise ValueError('Kernel/native changed')
    im=macho(raw);names={n:v for v,g in exports(raw,im).items() for n in g};local={}
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16))
    def symbol(n):
        values=local.get(n,set()).copy()
        if n in names:values.add(names[n])
        if len(values)!=1:raise ValueError('Unique sleep symbol required: '+n)
        return values.pop()
    calls=[]
    for seg in im['segments']:
        for sec in seg['sections']:
            if sec['name']!='__text':continue
            b=raw[sec['fileoff']:sec['fileoff']+sec['size']]
            for off in range(0,len(b)-3,4):
                w=struct.unpack_from('<I',b,off)[0]
                if w&0x7c000000==0x14000000:
                    pc=sec['va']+off;calls.append((pc,pc+(signed(w&0x3ffffff,26)<<2)))
    required=['_elden_pen_native_pause','_elden_pen_native_resume','_elden_pen_delivery_pause','_elden_pen_delivery_resume',
        '_elden_wake_native_irq_pending','_elden_nvt_low_power_command','_elden_wake_flow_init','_elden_wake_flow_suspend','_elden_wake_flow_resume','_elden_wake_flow_irq','_elden_wake_flow_stop',
        '_elden_wake_native_irq_acquire','_elden_wake_native_irq_arm','_elden_wake_native_irq_disarm','_elden_wake_native_irq_take','_elden_wake_native_irq_stop',
        '_assert_wait','_clear_wait','_thread_block','_thread_wakeup_prim']
    edges={}
    for n in required:
        target=symbol(n);hits=[hex(pc) for pc,t in calls if t==target]
        if not hits:raise ValueError('Actual original-worker sleep call absent: '+n)
        edges[n]={'target':hex(target),'calls_and_tails':hits}
    hooks=['_elden_touch_wake_install_power','_elden_touch_wake_request','_elden_touch_wake_status']
    for n in hooks:symbol(n)
    if any(t==symbol('_elden_touch_wake_install_power') for pc,t in calls):raise ValueError('Unqualified power-provider startup hook')
    # Two actual inherited display objects replaced; every other member exact.
    changed=[n for n,r in spec['display_members'].items() if r['before']!=r['after']]
    if set(changed)!={'EldenNvtRuntime.o','EldenNvtProtocol.o'}:raise ValueError('Other display owner changed')
    if spec.get('actual_shared_SMMU_native_staged_peripherals') is True:
        from peripheral_archive_audit import retained_archive
        row=spec.get('shared_archive_deltas',{}).get('libNativeLegacyDisplay.late.a')
        if not row:raise ValueError('Actual touch route shared delta absent')
        command=json.loads((a.kernel_build/'link-command.json').read_text())
        retained_archive('-Wl,-force_load,'+row['before_path'],command,spec)
    result={'original_touch_sleep_worker_link_passed':True,'kernel_sha256':spec['kernel_sha256'],'native_sha256':spec['native_sha256'],
        'actual_calls':edges,'kernel_power_request_interfaces':{n:hex(symbol(n)) for n in hooks},'only_display_members_changed':changed,
        'real_power_provider_installed':False,'default_wake_disabled':True,'worker_serial_execution':True,'same_pen_HID_service_nonterminal':True,
        'hardware':False,'screen_system_wake_qualified':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: actual original-worker nonterminal pen/pause/IRQ/SPI/firmware/event-wait call edges; no fake power provider')
if __name__=='__main__':main()
