"""Audit a session-linked camera image: required leaf edges inside owner methods."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
from peripheral_archive_audit import retained_archive
from camera_irq_input_audit import retained_camera_irq_input
from replay_xnu_objects import global_symbols
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
SESSION={n:'_elden_camera_capture_session_'+n for n in ('bind','begin','publish','serve','stop','shutdown')}
CLIENT_CLOSE='__ZN17EldenCameraClient11clientCloseEv';CLIENT_FREE='__ZN17EldenCameraClient4freeEv'
CLIENT_ANY='EldenCameraClient::*'   # any method range of the real client class
# Required leaf edges: each leaf's mandatory owner methods must each show at
# least one direct/tail call, every site must lie inside an allowed owner,
# and an empty allowed set means the leaf must have no caller at all (the
# retained sensor-only withdraw must never be used by this composition).
REQUIRED={
 '_elden_camera_dma_capture_prepare':({SESSION['begin']},None),
 '_elden_camera_dma_capture_probe':({SESSION['begin']},None),
 '_elden_camera_dma_capture_queue_initial':({SESSION['begin']},None),
 '_elden_camera_dma_capture_start':({SESSION['begin']},None),
 '_elden_camera_output_open':({SESSION['begin']},None),
 '_elden_camera_output_enable_irq':({SESSION['begin']},None),
 '_elden_camera_output_pump_irq':({SESSION['serve']},None),
 '_elden_camera_service_publish':({SESSION['publish']},None),
 '_elden_camera_output_close':({SESSION['stop'],SESSION['shutdown'],CLIENT_CLOSE,CLIENT_FREE},None),
 '_elden_camera_dma_capture_stop':({SESSION['stop'],SESSION['shutdown']},None),
 '_elden_camera_dma_capture_shutdown':({SESSION['shutdown']},None),
 '_elden_camera_output_stop':({SESSION['shutdown'],'_elden_camera_service_withdraw',
    '_elden_camera_service_withdraw_output'},None),
 '_elden_camera_service_output':({SESSION['shutdown']},None),
 '_elden_camera_service_withdraw_output':({SESSION['shutdown']},None),
 '_elden_camera_service_withdraw':(set(),set()),
 '_elden_camera_output_release':({CLIENT_CLOSE,CLIENT_FREE},{CLIENT_ANY}),
}
# Approved direct camera replacements and the single new session object.
REPLACED={'NativePipeline.o','FrameOutput.o','Service.o'}
NEW={'CaptureSession.o'}
# Leaf symbols must be defined inside these exact link-input objects.
EXPECTED_DEFINERS={
 'CaptureSession.o':set(SESSION.values()),
 'Service.o':{'_elden_camera_service_publish','_elden_camera_service_withdraw',
    '_elden_camera_service_output','_elden_camera_service_withdraw_output'},
 'FrameOutput.o':{'_elden_camera_output_open','_elden_camera_output_close','_elden_camera_output_stop',
    '_elden_camera_output_enable_irq','_elden_camera_output_pump_irq'},
 'CameraDmaBinding.o':{'_elden_camera_dma_capture_prepare','_elden_camera_dma_capture_probe',
    '_elden_camera_dma_capture_queue_initial','_elden_camera_dma_capture_start',
    '_elden_camera_dma_capture_stop','_elden_camera_dma_capture_shutdown'},
}
def check_required_edges(calls,bounds_of,symbol_of,required=REQUIRED):
    """Every mandatory owner method must contribute >=1 direct/tail call to
    its leaf; every site must lie inside an allowed owner range. A leaf whose
    allowed set is empty must have no call sites at all."""
    report={}
    for leaf,pair in required.items():
        mandatory,allowed=pair;owners=mandatory|(allowed or set());target=symbol_of(leaf)
        hits=[pc for pc,t in calls if t==target]
        boxes=[(o,b) for o in owners for b in bounds_of(o)]
        seen={o:0 for o in mandatory}
        for pc in hits:
            inside=[o for o,(a0,end) in boxes if a0<=pc<end]
            if not inside:raise ValueError('Unqualified camera leaf caller: '+leaf)
            for o in inside:
                if o in seen:seen[o]+=1
        missing=[o for o,n in seen.items() if not n]
        if missing:raise ValueError('Mandatory camera leaf edge absent: '+leaf+' <- '+','.join(sorted(missing)))
        report[leaf]={'call_sites':len(hits),'mandatory_owners':{o:seen[o] for o in sorted(mandatory)}}
    return report
def check_link_delta(before,after,report,session_build):
    """Retained parent command inputs must be byte-identical; only the exact
    approved camera replacements plus one new CaptureSession.o may differ."""
    def entries(command):
        out=[]
        for v in command:
            if not isinstance(v,str):continue
            p=Path(v.split(',',2)[2] if v.startswith('-Wl,-force_load,') else v.split(',')[-1])
            if p.suffix in ('.o','.a') and p.is_file():out.append(p)
        return out
    for v in before:
        path=Path(v.split(',')[-1])
        if v.startswith('-Wl,-force_load,'):retained_archive(v,after,report)
        elif path.is_file() and path.suffix in ('.a','.o'):
            if report.get('actual_original_camera_IRQ_executor') is True and path.name in REPLACED:
                if path.name=='Service.o':approved_replacement(path,after,report)
                else:retained_camera_irq_input(path,after,report)
                continue
            if after.count(v)!=1 or sha(path)!=report['inherited_inputs'].get(str(path)):
                raise ValueError('Original peer native object changed')
    before_paths={str(p) for p in entries(before)}
    for p in entries(after):
        if str(p) in before_paths:continue
        if p.name not in REPLACED|NEW:raise ValueError('Unapproved new link input: '+p.name)
    new=[v for v in after if isinstance(v,str) and Path(v.split(',')[-1]).name=='CaptureSession.o']
    if len(new)!=1:raise ValueError('Exactly one CaptureSession.o link input required')
    path=Path(new[0].split(',')[-1])
    if sha(path)!=session_build['object_sha256']:raise ValueError('Linked session object identity differs')
    return path
def approved_replacement(path,after,report):
    """Service.o as an exact declared source-built camera replacement; the
    same receipt fields retained_camera_irq_input requires for the others."""
    row=report.get('camera_irq_direct_deltas',{}).get('Service.o')
    if not isinstance(row,dict) or row.get('before_path')!=str(path):
        raise ValueError('Exact original camera service object path not declared')
    replacement=Path(row.get('after_path',''))
    if replacement.name!=path.name or not replacement.is_file():
        raise ValueError('Exact camera service replacement file absent')
    old=report.get('inherited_inputs',{}).get(str(path))
    if old is None or sha(path)!=old or row.get('before')!=old:
        raise ValueError('Original camera service object fingerprint changed')
    new=report.get('inherited_inputs',{}).get(str(replacement))
    if new is None or sha(replacement)!=new or row.get('after')!=new:
        raise ValueError('Replacement camera service object fingerprint changed')
    if after.count(str(path)) or after.count(str(replacement))!=1:
        raise ValueError('Original/replacement camera service input duplicated or absent')
    if row.get('source_build_sha256')!=report.get('camera_irq_builds',{}).get('Service.o'):
        raise ValueError('Camera service source-build receipt differs')
    return row
def check_object_identity(after):
    """Each leaf must be defined inside its exact expected link-input object."""
    resolved={}
    for v in after:
        if not isinstance(v,str):continue
        p=Path(v.split(',')[-1])
        if p.suffix in ('.o','.a') and p.is_file() and p.name in EXPECTED_DEFINERS:resolved[p.name]=p
    for name,leaves in EXPECTED_DEFINERS.items():
        if name not in resolved:raise ValueError('Expected defining link input absent: '+name)
        syms=global_symbols(resolved[name]);defined={n for n,v in syms.items() if v[0]&0xe}
        missing=leaves-defined
        if missing:raise ValueError('Leaf not defined in '+name+': '+','.join(sorted(missing)))
    return resolved
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('kernel-build','parent','session-build','prelink-tool','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports
    r=json.loads((a.kernel_build/'result.json').read_text());prior=json.loads((a.parent/'result.json').read_text())
    build=json.loads((a.session_build/'result.json').read_text())
    for folder,record in ((a.kernel_build,r),(a.parent,prior)):
        for n in ('kernel','native'):
            if sha(folder/(n+'.macho'))!=record[n+'_sha256']:raise ValueError('Linked source identity changed: '+n)
    if build.get('capture_session_object')!='CaptureSession.o' or build.get('automatic_startup') is not False:
        raise ValueError('Session build profile absent')
    link=json.loads((a.kernel_build/'link-command.json').read_text())
    before=json.loads((a.parent/'link-command.json').read_text())
    session_obj=check_link_delta(before,link,r,build);resolved=check_object_identity(link)
    raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes();im=macho(raw);oi=macho(old)
    names={n:v for v,g in exports(raw,im).items() for n in g};local={};starts=[]
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStTdDbB]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16));starts.append((int(m[1],16),m[2]))
    starts.sort()
    def symbol(n):
        v=local.get(n,set()).copy()
        if n in names:v.add(names[n])
        if len(v)!=1:raise ValueError('Unique camera session symbol missing: '+n)
        return v.pop()
    def bounds(n):
        a0=symbol(n);end=min((pc for pc,_ in starts if pc>a0),default=None)
        if end is None:raise ValueError('Session method boundary missing: '+n)
        return (a0,end)
    boxes={}
    def bounds_of(n):
        if n==CLIENT_ANY:
            return [(pc,starts[i+1][0] if i+1<len(starts) else pc+0x100000)
                for i,(pc,nm) in enumerate(starts) if re.match(r'__ZNK?17EldenCameraClient',nm)]
        if n not in boxes:boxes[n]=bounds(n)
        return [boxes[n]]
    calls=[];init=[];previous=[]
    for image,body,table in ((im,raw,init),(oi,old,previous)):
        for seg in image['segments']:
            for sec in seg['sections']:
                b=body[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table+=list(struct.unpack('<%dQ'%(len(b)//8),b))
                if image is im and sec['name']=='__text':
                    for off in range(0,len(b)-3,4):
                        word=struct.unpack_from('<I',b,off)[0]
                        if word&0x7c000000==0x14000000:pc=sec['va']+off;calls.append((pc,pc+(signed(word&0x3ffffff,26)<<2)))
    if len(init)!=len(previous):raise ValueError('New automatic session initializer')
    report=check_required_edges(calls,bounds_of,symbol)
    for method in SESSION.values():
        if any(t==symbol(method) for pc,t in calls):raise ValueError('Automatic incoming caller of session method: '+method)
    if '__GLOBAL__sub_I_CaptureSession' in local or '__GLOBAL__sub_I_CaptureSession' in names:
        raise ValueError('Automatic session static initializer')
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    proof={'actual_camera_capture_session_link':True,'session_object_sha256':build['object_sha256'],
        'session_object':str(session_obj.resolve()),'session_methods_zero_incoming_callers':True,
        'leaf_callers':report,'approved_replacements':sorted(REPLACED),'defining_objects':{k:str(v) for k,v in resolved.items()},
        'mod_init_entries':len(init),'kernel_sha256':sha(a.kernel_build/'kernel.macho'),
        'native_sha256':sha(a.kernel_build/'native.macho'),'automatic_startup':False,'physical':False,'hardware':False}
    (out/'session-link-validation.json').write_text(json.dumps(proof,indent=2)+'\n',newline='\n')
    print('PASS camera session link boundary: '+str(len(report))+' leaf APIs edge-verified, 6 session entries caller-free')
if __name__=='__main__':main()
