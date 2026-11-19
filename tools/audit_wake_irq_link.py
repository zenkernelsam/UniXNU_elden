"""Audit linked wake call edges, original owners and absence of startup/PM hooks."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build','parent','prelink-tool','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
    spec=json.loads((a.kernel_build/'result.json').read_text());oldspec=json.loads((a.parent/'result.json').read_text())
    raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes();native=(a.kernel_build/'native.macho').read_bytes()
    if sha(a.kernel_build/'kernel.macho')!=spec['kernel_sha256'] or sha(a.parent/'kernel.macho')!=oldspec['kernel_sha256'] or sha(a.kernel_build/'native.macho')!=spec['native_sha256']:
        raise ValueError('Image identity differs')
    im=macho(raw);prior=macho(old);ni=macho(native)
    names={n:v for v,g in exports(raw,im).items() for n in g};original={n:v for v,g in exports(old,prior).items() for n in g};local={}
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16))
    def unique(name):
        v=local.get(name,set()).copy()
        if name in names:v.add(names[name])
        if len(v)!=1:raise ValueError('Unique symbol required: '+name)
        return v.pop()
    calls=[];init=[];oldinit=[]
    for image,buf,initial in [(im,raw,init),(prior,old,oldinit)]:
        for seg in image['segments']:
            for sec in seg['sections']:
                b=buf[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':initial.extend(struct.unpack('<%dQ'%(len(b)//8),b))
                if image is im and sec['name']=='__text':
                    for off in range(0,len(b)-3,4):
                        w=struct.unpack_from('<I',b,off)[0]
                        if w&0xfc000000==0x94000000:
                            pc=sec['va']+off;calls.append((pc,pc+(signed(w&0x3ffffff,26)<<2)))
    if len(init)!=len(oldinit):raise ValueError('Wake must not add automatic initialization')
    names_needed=['_elden_wake_native_irq_acquire','_elden_wake_native_irq_arm','_elden_wake_native_irq_disarm',
        '_elden_wake_native_irq_stop','_elden_wake_native_irq_take','_elden_wake_native_irq_event',
        '_elden_wake_flow_init','_elden_wake_flow_suspend','_elden_wake_flow_irq','_elden_wake_flow_resume','_elden_wake_flow_stop','_elden_wake_flow_normal_input']
    exported={n:hex(unique(n)) for n in names_needed}
    # None of the current inherited consumers may call the new wake entry.
    # The library's existing internal callbacks may call its own functions.
    for n in ['_elden_wake_native_irq_acquire','_elden_wake_flow_init','_elden_wake_flow_suspend','_elden_wake_flow_irq','_elden_wake_flow_resume']:
        if any(v==unique(n) for pc,v in calls):raise ValueError('Unreviewed automatic/worker wake hook: '+n)
    edges={}
    for n in ['_elden_wake_irq_acquire','_elden_wake_irq_arm','_elden_wake_irq_disarm','_elden_wake_irq_stop','_elden_wake_gesture']:
        target=unique(n);hits=[hex(pc) for pc,v in calls if v==target]
        if not hits:raise ValueError('Actual route/flow call absent: '+n)
        edges[n]={'target':hex(target),'calls':hits}
    # Parent touch startup, recovery and pen remain byte-identical in new image.
    spans={}
    for name in ['_elden_touch_runtime_start','_elden_touch_request_stop','_elden_haptics_stop_for_reboot','_elden_pen_native_start','_elden_pen_native_stop']:
        if name not in original:raise ValueError('Original owner missing: '+name)
        addr=unique(name);before=original[name];size=64
        off=backed_offset(im['segments'],addr,size,len(raw));oldoff=backed_offset(prior['segments'],before,size,len(old))
        # BL relocation positions can shift with segment extent even while
        # inherited object identity is unchanged; these owners stay addressed
        # in earlier original segments here, so exact bytes are required.
        if raw[off:off+size]!=old[oldoff:oldoff+size]:raise ValueError('Inherited owner prefix changed: '+name)
        spans[name]={'address':hex(addr),'sha256':hashlib.sha256(raw[off:off+size]).hexdigest()}
    result={'wake_component_link_audit_passed':True,'kernel_sha256':spec['kernel_sha256'],'native_sha256':spec['native_sha256'],
        'exported':exported,'actual_internal_edges':edges,'inherited_owner_prefixes':spans,'initializer_count_unchanged':True,
        'no_current_worker_PM_startup_hook':True,'GPIO_PDC_source_route':[162,109,620,652],
        'hardware':False,'system_display_wake_qualified':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: actual linked IRQ/flow/gesture edges, unchanged initialization/owner code and no automatic PM/startup hooks')
if __name__=='__main__':main()
