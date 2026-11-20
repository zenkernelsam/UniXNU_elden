"""Audit actual linked vnode/SHA/cache/RPC paths and absence of unqualified start."""
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
    r=json.loads((a.kernel_build/'result.json').read_text());parent=json.loads((a.parent/'result.json').read_text());raw=(a.kernel_build/'kernel.macho').read_bytes();old=(a.parent/'kernel.macho').read_bytes()
    if sha(a.kernel_build/'kernel.macho')!=r['kernel_sha256'] or sha(a.kernel_build/'native.macho')!=r['native_sha256'] or sha(a.parent/'kernel.macho')!=parent['kernel_sha256']:raise ValueError('Image identity differs')
    im=macho(raw);oi=macho(old);names={n:v for v,g in exports(raw,im).items() for n in g};local={}
    for line in subprocess.check_output(['xcrun','nm','-n',a.kernel_build/'native.macho'],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:local.setdefault(m[2],set()).add(int(m[1],16))
    def symbol(n):
        values=local.get(n,set()).copy()
        if n in names:values.add(names[n])
        if len(values)!=1:raise ValueError('One actual pack symbol required: '+n)
        return values.pop()
    calls=[];initial=[];previous=[]
    for image,blob,table in [(im,raw,initial),(oi,old,previous)]:
        for seg in image['segments']:
            for sec in seg['sections']:
                b=blob[sec['fileoff']:sec['fileoff']+sec['size']]
                if sec['name']=='__mod_init_func':table.extend(struct.unpack('<%dQ'%(len(b)//8),b))
                if image is im and sec['name']=='__text':
                    for off in range(0,len(b)-3,4):
                        w=struct.unpack_from('<I',b,off)[0]
                        if w&0x7c000000==0x14000000:
                            pc=sec['va']+off;calls.append((pc,pc+(signed(w&0x3ffffff,26)<<2)))
    if len(initial)!=len(previous):raise ValueError('Automatic file/pack initializer added')
    runtime=r.get('actual_sensor_runtime_composition') is True
    explicit=explicit_binding_calls('_elden_sensor_pack_binding_open',calls,symbol,a.kernel_build/'native.macho',runtime)
    if explicit:no_outer_sensor_start(calls,symbol,runtime,a.kernel_build/'native.macho')
    edges={}
    for n in ('_elden_sensor_pack_native_bind','_elden_sensor_pack_native_size','_elden_sensor_pack_native_read','_elden_sensor_pack_native_stop',
              '_elden_sensor_rpc_open','_elden_sensor_rpc_step','_elden_sensor_rpc_request_stop','_elden_sensor_rpc_finish',
              '_vnode_open','_vnode_getattr','_vnode_isreg','_vn_rdwr','_vnode_close','_SHA256_Init','_SHA256_Update','_SHA256_Final',
              '_elden_sensor_pack_header','_elden_sensor_pack_open','_elden_sensor_pack_find','_elden_sensor_pack_entry'):
        target=symbol(n);hits=[hex(pc) for pc,t in calls if t==target]
        if not hits:raise ValueError('Actual native file/RPC edge missing: '+n)
        edges[n]={'target':hex(target),'calls_and_tails':hits}
    original=json.loads((a.parent/'link-command.json').read_text());current=json.loads((a.kernel_build/'link-command.json').read_text())
    retained={}
    for value in original:
        if value.startswith('-Wl,-force_load,'):retained[Path(value.split(',',2)[2]).name]=retained_archive(value,current,r)
    result={'native_pack_component_link_passed':True,'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],'actual_native_file_hash_cache_RPC_edges':edges,
            'no_native_startup':True,'explicit_memory_to_pack_binding_calls':explicit,'initializer_count_retained':len(initial),'original_all_archive_inputs_retained':all(v['unchanged_path'] for v in retained.values()),'actual_retained_archive_checks':retained,
            'actual_Y700_Android_capture_or_DMA_provider':False,'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS: actual VFS/SHA256/cache→RPC calls, original owners/initializers retained, no unqualified startup')
if __name__=='__main__':main()
