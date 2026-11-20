"""Audit actual shared camera/backlight call boundaries, archive and OS17 ABI."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
from peripheral_archive_audit import retained_archive
from camera_startup_audit import check_camera_startup
from build_camera_power import check_backlight_abi

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('kernel-build','parent','power-build','prelink-tool','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('kernel_build','parent','power_build','prelink_tool'):setattr(a,name,getattr(a,name).resolve(strict=True))
    report=json.loads((a.kernel_build/'result.json').read_text());prior=json.loads((a.parent/'result.json').read_text());build=json.loads((a.power_build/'result.json').read_text())
    for folder,r in ((a.kernel_build,report),(a.parent,prior)):
        if sha(folder/'kernel.macho')!=r['kernel_sha256'] or sha(folder/'native.macho')!=r['native_sha256']:raise ValueError('Actual linked image differs')
    if not report.get('one_original_I2c10_GENI_owner') or not report.get('actual_camera_WL_power_session'):raise ValueError('Shared camera profile absent')
    sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports,backed_offset
    raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes();im=macho(raw);oi=macho(old)
    names={n:v for v,g in exports(raw,im).items() for n in g};local={};starts=[]
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStTdDbB]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16));starts.append((int(m[1],16),m[2]))
    def symbol(name):
        values=local.get(name,set()).copy()
        if name in names:values.add(names[name])
        if len(values)!=1:raise ValueError('Unique actual camera power symbol absent: '+name)
        return values.pop()
    calls=[];initial=[];before=[]
    for image,body,table in ((im,raw,initial),(oi,old,before)):
        for seg in image['segments']:
            for sec in seg['sections']:
                data=body[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table.extend(struct.unpack('<%dQ'%(len(data)//8),data))
                if image is im and sec['name']=='__text':
                    for off in range(0,len(data)-3,4):
                        word=struct.unpack_from('<I',data,off)[0]
                        if word&0x7c000000==0x14000000:
                            pc=sec['va']+off;calls.append((pc,pc+(signed(word&0x3ffffff,26)<<2)))
    if len(initial)!=len(before):raise ValueError('Automatic camera/backlight initializer added')
    startup=check_camera_startup(calls,symbol,a.kernel_build/'native.macho',True)
    def edge(caller,callee,empty=False):
        lo=symbol(caller);hi=min((v for v,n in starts if v>lo),default=None)
        hits=[pc for pc,target in calls if target==symbol(callee)]
        if hi is None or (not hits and not empty) or any(not lo<=pc<hi for pc in hits):raise ValueError('Actual owner call outside expected function: '+callee)
        return {'caller':caller,'start':hex(lo),'next':hex(hi),'target':hex(symbol(callee)),'calls':[hex(v) for v in hits]}
    boundaries={}
    for caller,callee in [('_elden_camera_power_session_open','_elden_camera_i2c10_open'),
        ('_elden_camera_power_bind_bus','_elden_camera_power_open'),('_elden_camera_power_session_resource','_elden_camera_power_resource'),
        ('_elden_camera_power_session_stop','_elden_camera_power_stop'),('_elden_camera_power_session_stop','_elden_camera_i2c10_close'),
        ('_elden_camera_powered_capture_stop','_elden_camera_capture_stop')]:boundaries[callee]=edge(caller,callee)
    for name in ('_elden_camera_power_session_open','_elden_camera_powered_capture_bind','_elden_camera_powered_capture_probe','_elden_camera_powered_capture_start'):
        if any(target==symbol(name) for pc,target in calls):raise ValueError('Unqualified outer camera power startup: '+name)
    for name in ('_elden_camera_i2c10_read','_elden_camera_i2c10_write','_elden_bl_platform_i2c10_read','_elden_bl_platform_i2c10_write'):
        if not any(target==symbol(name) for pc,target in calls):raise ValueError('Actual regulator/shared transfer edge absent: '+name)
    oldcmd=json.loads((a.parent/'link-command.json').read_text());cmd=json.loads((a.kernel_build/'link-command.json').read_text());retained={}
    for value in oldcmd:
        path=Path(value.split(',')[-1])
        if value.startswith('-Wl,-force_load,'):retained[str(path)]=retained_archive(value,cmd,report)
        elif path.is_file() and path.suffix in ('.o','.a'):
            if cmd.count(value)!=1 or sha(path)!=report['inherited_inputs'].get(str(path)):raise ValueError('Original nonarchive input changed')
    if sum(Path(v.split(',')[-1]).name=='libEldenBacklight.a' for v in cmd)!=1:raise ValueError('Competing I2C10/backlight owner')
    for name,row in build['native_objects'].items():
        if sha(a.power_build/name)!=row['sha256']:raise ValueError('Built native object changed')
    abi=check_backlight_abi(a.power_build/'BacklightRuntime.late.o',a.power_build/'original-members/BacklightRuntime.late.o')
    # Both original inherited IOService table and new class actual compiled
    # targets are checked by the object ABI gate; all native maps remain the
    # original Region methods, with their original 32bit LDR/STR/barrier.
    oldnames={n:v for v,g in exports(old,oi).items() for n in g}
    originals={}
    for name in ('__ZN17AppleARMBacklightC2EPK11OSMetaClass','__ZN17AppleARMBacklightD2Ev','__ZN17AppleARMBacklight10gMetaClassE'):
        if symbol(name)!=oldnames[name]:raise ValueError('Original Apple backlight absolute import moved')
        originals[name]=hex(symbol(name))
    metadata=json.loads((a.kernel_build/'kernel.json').read_text());flags={n:metadata.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}
    if any(v is not False for v in flags.values()):raise ValueError('Global experimental qualification changed')
    result={'passed':True,'kernel_sha256':report['kernel_sha256'],'native_sha256':report['native_sha256'],'actual_call_boundaries':boundaries,
        'actual_capture_startup_boundaries':startup,'archive_checks':retained,'backlight_ABI':abi,'original_Apple_bindings':originals,
        'same_initializer_count':True,'outer_power_capture_no_startup':True,'one_original_I2c10_owner':True,'global_flags':flags,
        'physical_power_clock_pin_route_frames_OSservice':False,'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS actual shared I2C10/WL/capture call bounds, genuine169slot backlight, original archives/recovery and absent automatic startup')
if __name__=='__main__':main()
