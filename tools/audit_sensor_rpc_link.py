"""Check actual FastRPC provider/core/file-service edges and no automatic start."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
from peripheral_archive_audit import retained_archive
from peripheral_startup_audit import explicit_binding_calls,no_outer_sensor_start
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signed(v,b):return v-(1<<b) if v&(1<<(b-1)) else v
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build','parent','prelink-tool','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports
    r=json.loads((a.kernel_build/'result.json').read_text());parent=json.loads((a.parent/'result.json').read_text())
    raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes();native=(a.kernel_build/'native.macho').read_bytes()
    if sha(a.kernel_build/'kernel.macho')!=r['kernel_sha256'] or sha(a.kernel_build/'native.macho')!=r['native_sha256'] or sha(a.parent/'kernel.macho')!=parent['kernel_sha256']:raise ValueError('RPC image identity differs')
    im=macho(raw);oi=macho(old);names={n:v for v,g in exports(raw,im).items() for n in g};local={}
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16))
    def symbol(n):
        values=local.get(n,set()).copy()
        if n in names:values.add(names[n])
        if len(values)!=1:raise ValueError('One actual RPC symbol required: '+n)
        return values.pop()
    calls=[];initial=[];previous=[]
    for image,b,table in [(im,raw,initial),(oi,old,previous)]:
        for seg in image['segments']:
            for sec in seg['sections']:
                data=b[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table.extend(struct.unpack('<%dQ'%(len(data)//8),data))
                if image is im and sec['name']=='__text':
                    for at in range(0,len(data)-3,4):
                        w=struct.unpack_from('<I',data,at)[0]
                        if w&0x7c000000==0x14000000:
                            pc=sec['va']+at;calls.append((pc,pc+(signed(w&0x3ffffff,26)<<2)))
    if len(initial)!=len(previous):raise ValueError('Automatic RPC initializer added')
    entries=['_elden_sensor_rpc_open','_elden_sensor_rpc_channel','_elden_sensor_rpc_receive','_elden_sensor_rpc_step','_elden_sensor_rpc_request_stop','_elden_sensor_rpc_finish','_elden_sensor_rpc_status']
    explicit={}
    for n in entries:
        explicit[n]=explicit_binding_calls(n,calls,symbol,a.kernel_build/'native.macho')
    if any(explicit.values()):
        memory_calls=explicit_binding_calls('_elden_sensor_pack_binding_open',calls,symbol,a.kernel_build/'native.macho')
        if memory_calls:no_outer_sensor_start(calls,symbol)
        else:
            pack=symbol('_elden_sensor_pack_binding_open')
            if any(t==pack for pc,t in calls):raise ValueError('Unqualified actual pack startup caller')
    edges={}
    for n in ['_elden_sensor_rpc_core_init','_elden_sensor_rpc_core_channel','_elden_sensor_rpc_core_receive','_elden_sensor_rpc_core_step',
              '_elden_sensor_pack_header','_elden_sensor_pack_open','_elden_sensor_pack_path','_elden_sensor_pack_find','_elden_sensor_pack_child']:
        target=symbol(n);hits=[hex(pc) for pc,t in calls if t==target]
        if not hits:raise ValueError('Actual provider/file-service edge absent: '+n)
        edges[n]={'target':hex(target),'calls_and_tails':hits}
    oldcmd=json.loads((a.parent/'link-command.json').read_text());cmd=json.loads((a.kernel_build/'link-command.json').read_text())
    retained={}
    for value in oldcmd:
        if value.startswith('-Wl,-force_load,'):retained[Path(value.split(',',2)[2]).name]=retained_archive(value,cmd,r)
    metadata=json.loads((a.kernel_build/'kernel.json').read_text());flags={n:metadata.get(n) for n in ('abi_qualified','loadable','runtime_qualified','instruction_relocation_complete')}
    if any(v is not False for v in flags.values()):raise ValueError('Experimental flags changed')
    result={'RPC_component_full_link_passed':True,'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],'actual_provider_core_file_calls':edges,
            'public_provider_entrypoints':{n:hex(symbol(n)) for n in entries},'actual_explicit_pack_binding_calls':explicit,'no_auto_start':True,'initializer_count_retained':len(initial),
            'original_all_peripheral_archives_retained':all(v['unchanged_path'] for v in retained.values()),'actual_retained_archive_checks':retained,'actual_GLINK_DMA_registry_calibration_provider':False,'physical_sensor_stream_UI_rotation':False,'hardware':False,'global_flags':flags}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: actual RPC-provider/core/file-service edges, no owner auto-start, original peripheral archives/initializer count retained')
if __name__=='__main__':main()
