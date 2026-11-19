"""Exercise actual ar/member and compiled-function rejection gates on Mac.

Temporary compiled objects are test-only. Real linked native symbols provide
the call-boundary checks; injected calls/declarations are explicit negatives.
"""
from pathlib import Path
import argparse,copy,hashlib,json,re,subprocess,sys
from peripheral_archive_audit import retained_archive,members
from peripheral_startup_audit import explicit_binding_calls,no_outer_sensor_start
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('kernel-build','prelink-tool','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);checks=0;tests=[]
    def run(cmd,label,cwd=None):
        r=subprocess.run(list(map(str,cmd)),cwd=cwd,capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed')
    def verify(label,fn,refuse=False):
        nonlocal checks
        try:result=fn()
        except ValueError as e:
            if not refuse:raise
            tests.append({'case':label,'refused':True,'reason':str(e)})
        else:
            if refuse:raise ValueError('Incorrectly accepted injected negative: '+label)
            tests.append({'case':label,'accepted':True})
        checks+=1
    for n,value in (('old',1),('new',2),('peer',3),('changed-peer',4)):
        source=out/(n+'.c');source.write_text('int '+n.replace('-','_')+'_test(void){return '+str(value)+';}\n')
        run(['xcrun','clang','-target','arm64-apple-ios17.0','-ffreestanding','-fno-builtin','-mgeneral-regs-only','-c',source,'-o',out/(n+'.o')],n+'-compile')
    old=out/'old/libEldenAdspRuntime.a';old.parent.mkdir();new=out/'new/libEldenAdspRuntime.a';new.parent.mkdir()
    for folder,obj in ((old.parent,out/'old.o'),(new.parent,out/'new.o')):
        (folder/'AdspProbe.o').write_bytes(obj.read_bytes());(folder/'peer.o').write_bytes((out/'peer.o').read_bytes())
        run(['ar','rcs',folder/'libEldenAdspRuntime.a','AdspProbe.o','peer.o'],folder.name+'-archive',folder)
    before=members(old);after=members(new);argument='-Wl,-force_load,'+str(old);command=['-Wl,-force_load,'+str(new)]
    record={'inherited_inputs':{str(old):sha(old)},'changed_transport_archives':{'libEldenAdspRuntime.a':{n:{'before':before[n],'after':after[n]} for n in before}}}
    verify('exact_original_member_replacement',lambda:retained_archive(argument,command,record))
    verify('unchanged_actual_SHA',lambda:retained_archive(argument,[argument],record))
    wrong=copy.deepcopy(record);wrong['inherited_inputs'][str(old)]='1'*64
    verify('original_SHA_mismatch',lambda:retained_archive(argument,command,wrong),True)
    wrong=copy.deepcopy(record);wrong['changed_transport_archives']['libEldenAdspRuntime.a']['AdspProbe.o']['after']='1'*64
    verify('fabricated_member_SHA',lambda:retained_archive(argument,command,wrong),True)
    verify('missing_archive_declaration',lambda:retained_archive(argument,command,{'inherited_inputs':record['inherited_inputs']}),True)
    verify('duplicate_replacement',lambda:retained_archive(argument,command+command,record),True)
    verify('replacement_absent',lambda:retained_archive(argument,[],record),True)
    changed=out/'bad/libEldenAdspRuntime.a';changed.parent.mkdir();(changed.parent/'AdspProbe.o').write_bytes((out/'new.o').read_bytes());(changed.parent/'peer.o').write_bytes((out/'changed-peer.o').read_bytes())
    run(['ar','rcs',changed,'AdspProbe.o','peer.o'],'changed-peer-archive',changed.parent)
    bad=members(changed);declared=copy.deepcopy(record)
    for n in bad:declared['changed_transport_archives']['libEldenAdspRuntime.a'][n]['after']=bad[n]
    verify('declared_unrelated_peer_change',lambda:retained_archive(argument,['-Wl,-force_load,'+str(changed)],declared),True)
    sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports
    raw=(a.kernel_build/'kernel.macho').read_bytes();image=macho(raw);symbols={n:v for v,g in exports(raw,image).items() for n in g}
    def symbol(n):
        if n not in symbols:raise ValueError('Actual compiled symbol absent: '+n)
        return symbols[n]
    native=a.kernel_build/'native.macho';target=symbol('_elden_sensor_pack_binding_open');start=symbol('_elden_sensor_memory_binding_open');end=symbol('_elden_sensor_memory_binding_channel')
    verify('actual_binding_function_allowed',lambda:explicit_binding_calls('_elden_sensor_pack_binding_open',[(start+4,target)],symbol,native))
    verify('before_actual_function_rejected',lambda:explicit_binding_calls('_elden_sensor_pack_binding_open',[(start-4,target)],symbol,native),True)
    verify('next_actual_function_rejected',lambda:explicit_binding_calls('_elden_sensor_pack_binding_open',[(end,target)],symbol,native),True)
    verify('outer_actual_startup_rejected',lambda:no_outer_sensor_start([(start+4,start)],symbol),True)
    stop=symbol('_elden_sensor_rpc_request_stop');finish=symbol('_elden_sensor_pack_binding_finish')
    verify('actual_finish_stop_allowed',lambda:explicit_binding_calls('_elden_sensor_rpc_request_stop',[(finish+4,stop)],symbol,native))
    verify('unrelated_actual_endpoint_stop_rejected',lambda:explicit_binding_calls('_elden_sensor_rpc_request_stop',[(symbol('_elden_sensor_adsp_endpoint_open')+4,stop)],symbol,native),True)
    result={'passed':True,'checks':checks,'actual_compiled_ar_members':True,'actual_native_function_symbols':True,'negative_calls_and_declarations_injected':True,
            'kernel_sha256':sha(a.kernel_build/'kernel.macho'),'native_sha256':sha(native),'tests':tests,'hardware':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'passed':True,'checks':checks,'negative_gates':True}))
if __name__=='__main__':main()
