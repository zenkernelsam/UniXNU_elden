"""Audit actual camera/CCI/native call chain and absence of unqualified startup."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build','parent','prelink-tool','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
    r=json.loads((a.kernel_build/'result.json').read_text());parent=json.loads((a.parent/'result.json').read_text())
    raw=(a.kernel_build/'kernel.macho').read_bytes();native=(a.kernel_build/'native.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes()
    if sha(a.kernel_build/'kernel.macho')!=r['kernel_sha256'] or sha(a.kernel_build/'native.macho')!=r['native_sha256'] or sha(a.parent/'kernel.macho')!=parent['kernel_sha256']:raise ValueError('Image identity differs')
    im=macho(raw);prior=macho(old);names={n:v for v,g in exports(raw,im).items() for n in g};oldnames={n:v for v,g in exports(old,prior).items() for n in g}
    calls=[];init=[];oldinit=[]
    branches=[]
    for image,b,initial in [(im,raw,init),(prior,old,oldinit)]:
        for seg in image['segments']:
            for sec in seg['sections']:
                block=b[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':initial.extend(struct.unpack('<%dQ'%(len(block)//8),block))
                if image is im and sec['name']=='__text':
                    for off in range(0,len(block)-3,4):
                        w=struct.unpack_from('<I',block,off)[0]
                        if w&0xfc000000==0x94000000:
                            pc=sec['va']+off;calls.append((pc,pc+(signed(w&0x3ffffff,26)<<2)))
                        if w&0x7c000000==0x14000000:
                            pc=sec['va']+off;branches.append((pc,pc+(signed(w&0x3ffffff,26)<<2)))
    if len(init)!=len(oldinit):raise ValueError('Automatic camera initialization added')
    def symbol(n):
        if n not in names:raise ValueError('Linked camera export missing: '+n)
        return names[n]
    required=['_elden_camera_native_cci_open','_elden_camera_native_cci_ready','_elden_camera_sensor_bind',
        '_elden_camera_sensor_probe','_elden_camera_sensor_start','_elden_camera_sensor_control','_elden_camera_sensor_stop','_elden_camera_sensor_shutdown',
        '_elden_camera_cci_init','_elden_camera_cci_read_register','_elden_camera_cci_write_register','_elden_camera_cci_stop','_elden_camera_cci_status',
        '_elden_camera_native_cci_read','_elden_camera_native_cci_write','_elden_camera_native_cci_stop','_elden_camera_probe','_elden_camera_start',
        '_elden_camera_control','_elden_camera_stop','_elden_camera_terminal']
    targets={n:hex(symbol(n)) for n in required};edges={}
    for n in required:
        hits=[hex(pc) for pc,v in branches if v==symbol(n)]
        if n not in ['_elden_camera_native_cci_open','_elden_camera_sensor_bind','_elden_camera_sensor_probe','_elden_camera_sensor_start','_elden_camera_sensor_control','_elden_camera_sensor_stop','_elden_camera_sensor_shutdown'] and not hits:
            raise ValueError('Actual native/sensor/CCI call absent: '+n)
        edges[n]=hits
    if edges['_elden_camera_native_cci_open']:raise ValueError('Unreviewed automatic camera owner startup')
    pipeline=r.get('actual_camera_pipeline_capture_binding') is True
    if edges['_elden_camera_sensor_bind']:
        if not pipeline:raise ValueError('Unreviewed automatic sensor startup')
        # Only the exact explicit capture-bind wrapper may call sensor bind;
        # actual outer native/capture opens remain uncalled in this image.
        symbols=[]
        for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
            m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
            if m:symbols.append((int(m[1],16),m[2]))
        start=symbol('_elden_camera_capture_bind');end=min((pc for pc,_ in symbols if pc>start),default=None)
        if end is None or any(not start<=int(pc,16)<end for pc in edges['_elden_camera_sensor_bind']):raise ValueError('Sensor bind call outside actual capture wrapper')
        for n in ('_elden_camera_capture_bind','_elden_camera_native_pipeline_open'):
            if any(target==symbol(n) for pc,target in branches):raise ValueError('Unqualified outer camera startup')
    # Public source control methods actually call native CCI; no binding stub.
    groups=[('_elden_camera_sensor_probe','_elden_camera_probe'),('_elden_camera_sensor_start','_elden_camera_start'),
        ('_elden_camera_sensor_control','_elden_camera_control'),('_elden_camera_sensor_shutdown','_elden_camera_stop')]
    # Tail branches are permitted; verify a target instruction in each wrapper.
    for owner,callee in groups:
        start=symbol(owner);found=False
        for off in range(0,128,4):
            pc=start+off;at=backed_offset(im['segments'],pc,4,len(raw));w=struct.unpack_from('<I',raw,at)[0]
            if w&0x7c000000==0x14000000 and pc+(signed(w&0x3ffffff,26)<<2)==symbol(callee):found=True;break
        if not found:raise ValueError('Camera binding actual call/tail absent: '+owner)
    def read_at(image,data,addr,size):
        off=backed_offset(image['segments'],addr,size,len(data));return data[off:off+size]
    def all_symbols(path):
        result={}
        for line in subprocess.check_output(['xcrun','nm','-n',path],text=True).splitlines():
            m=re.match(r'^([0-9a-fA-F]+)\s+[sStTdDbB]\s+(\S+)$',line)
            if m:result.setdefault(m[2],set()).add(int(m[1],16))
        return result
    oldsymbols=all_symbols(a.parent/'native.macho');newsymbols=all_symbols(a.kernel_build/'native.macho')
    def same_target(before,after):
        # Pooled strings carry identical content, not just an arbitrary nearby
        # symbol; real globals bind the nearest original label and same offset.
        def cstring(image,data,addr):
            try:b=read_at(image,data,addr,256)
            except ValueError:return None
            end=b.find(b'\0')
            if end<=0 or any(v<32 or v>126 for v in b[:end]):return None
            return b[:end+1]
        x=cstring(prior,old,before);y=cstring(im,raw,after)
        if x is not None and x==y:return {'cstring_sha256':hashlib.sha256(x).hexdigest(),'bytes':len(x)}
        closest=max(v for values in oldsymbols.values() for v in values if v<=before)
        delta=before-closest
        labels=[n for n,values in oldsymbols.items() if closest in values and n in newsymbols and after-delta in newsymbols[n]]
        if labels and delta<=4096:return {'symbols':sorted(labels),'symbol_offset':delta}
        raise ValueError('Relocated owner data target identity differs')
    def adrp_target(word,pc):
        imm=((word>>5)&0x7ffff)<<2|((word>>29)&3);return (pc&~4095)+(signed(imm,21)<<12)
    preserved={}
    for n in ['_elden_touch_runtime_start','_elden_haptics_stop_for_reboot','_elden_pen_native_start','_elden_rtc_stop_for_reboot']:
        before=oldnames[n];after=symbol(n);size=64
        at=backed_offset(im['segments'],after,size,len(raw));oldat=backed_offset(prior['segments'],before,size,len(old))
        changed=[];allowed_add=set()
        for offset in range(0,size,4):
            x=struct.unpack_from('<I',old,oldat+offset)[0];y=struct.unpack_from('<I',raw,at+offset)[0]
            # An identical ADRP can still have a different ADD low12 when
            # string pooling moves within its page. Qualify the whole pair.
            if x==y and x&0x9f000000==0x90000000 and offset+4<size:
                nx=struct.unpack('<I',read_at(prior,old,before+offset+4,4))[0]
                ny=struct.unpack('<I',read_at(im,raw,after+offset+4,4))[0]
                if nx!=ny and nx&0xffc00000==0x91000000 and ny&0xffc00000==0x91000000 and (nx&0x3ff)==(ny&0x3ff) and (nx>>5)&31==x&31:
                    target_old=adrp_target(x,before+offset)+((nx>>10)&4095)
                    target_new=adrp_target(y,after+offset)+((ny>>10)&4095)
                    changed.append({'instruction_offset':offset,'target_before':hex(target_old),'target_after':hex(target_new),**same_target(target_old,target_new)})
                    allowed_add.add(offset+4)
            if x==y or offset in allowed_add:continue
            if x&0x7c000000==0x14000000 and x&0xfc000000==y&0xfc000000:
                target_old=before+offset+(signed(x&0x3ffffff,26)<<2)
                target_new=after+offset+(signed(y&0x3ffffff,26)<<2)
                labels=[label for label,values in oldsymbols.items() if target_old in values and label in newsymbols and target_new in newsymbols[label]]
                if not labels:raise ValueError('Inherited owner branch target differs: '+n)
                changed.append({'instruction_offset':offset,'branch_target_before':hex(target_old),'branch_target_after':hex(target_new),'exact_target_symbols':sorted(labels)})
            elif x&0x9f000000==0x90000000 and y&0x9f000000==0x90000000 and x&31==y&31:
                next_old=struct.unpack('<I',read_at(prior,old,before+offset+4,4))[0]
                next_new=struct.unpack('<I',read_at(im,raw,after+offset+4,4))[0]
                rd=x&31;target_old=adrp_target(x,before+offset);target_new=adrp_target(y,after+offset)
                if next_old&0xffc00000==0x91000000 and next_new&0xffc00000==0x91000000 and (next_old&0x3ff)==(next_new&0x3ff) and (next_old>>5)&31==rd:
                    target_old+=(next_old>>10)&4095;target_new+=(next_new>>10)&4095;allowed_add.add(offset+4)
                elif next_old==next_new and next_old&0x3b000000==0x39000000 and (next_old>>5)&31==rd:
                    shift=(next_old>>30)&3;target_old+=((next_old>>10)&4095)<<shift;target_new+=((next_new>>10)&4095)<<shift
                else:raise ValueError('Owner ADRP consumer differs: '+n)
                changed.append({'instruction_offset':offset,'target_before':hex(target_old),'target_after':hex(target_new),**same_target(target_old,target_new)})
            else:raise ValueError('Inherited owner instruction changed beyond qualified ADRP/data relocation: '+n)
        preserved[n]={'address':hex(after),'instruction_prefix_bytes':size,'qualified_data_relocations':changed}
    result={'camera_CCI_component_link_audit_passed':True,'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],
        'exports':targets,'actual_direct_calls_and_tail_branches':edges,'unchanged_initializer_count':True,'no_automatic_camera_startup':True,
        'preserved_original_owners':preserved,'camera_frames_verified':False,'physical_power_CSI_VFE_DMA_provider':False,'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: actual source sensor/native CCI call chain, unchanged owners/initializers and no automatic camera startup')
if __name__=='__main__':main()
