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
    # Actual three-member QRTR layout replacement requires its exact profile;
    # earlier ADSP-only records retain the original single-member boundary.
    old3=out/'qrtr-old/libEldenAdspRuntime.a';new3=out/'qrtr-new/libEldenAdspRuntime.a'
    old3.parent.mkdir();new3.parent.mkdir()
    for folder,probe in ((old3.parent,out/'old.o'),(new3.parent,out/'new.o')):
        (folder/'AdspProbe.o').write_bytes(probe.read_bytes())
        (folder/'ShengQrtr.o').write_bytes((out/('peer.o' if folder==old3.parent else 'changed-peer.o')).read_bytes())
        (folder/'QcomQrtr.o').write_bytes((out/('old.o' if folder==old3.parent else 'new.o')).read_bytes())
        (folder/'peer.o').write_bytes((out/'peer.o').read_bytes())
        run(['ar','rcs',folder/'libEldenAdspRuntime.a','AdspProbe.o','ShengQrtr.o','QcomQrtr.o','peer.o'],folder.name+'-archive',folder)
    b3=members(old3);n3=members(new3);arg3='-Wl,-force_load,'+str(old3);cmd3=['-Wl,-force_load,'+str(new3)]
    r3={'inherited_inputs':{str(old3):sha(old3)},'changed_transport_archives':{'libEldenAdspRuntime.a':{n:{'before':b3[n],'after':n3[n]} for n in b3}},'single_original_ADSP_IPCRTR_QRTR_owner':True}
    verify('exact_three_QRTR_consumers',lambda:retained_archive(arg3,cmd3,r3))
    negative=copy.deepcopy(r3);negative.pop('single_original_ADSP_IPCRTR_QRTR_owner')
    verify('three_consumers_without_profile_refused',lambda:retained_archive(arg3,cmd3,negative),True)
    chain=copy.deepcopy(r3);chain['cumulative_transport_archives']=copy.deepcopy(r3['changed_transport_archives']);chain['changed_transport_archives']['libEldenAdspRuntime.a']['AdspProbe.o']['before']='f'*64
    verify('exact_cumulative_original_members',lambda:retained_archive(arg3,cmd3,chain))
    chain['cumulative_transport_archives']['libEldenAdspRuntime.a']['QcomQrtr.o']['after']='f'*64
    verify('counterfeit_cumulative_members_refused',lambda:retained_archive(arg3,cmd3,chain),True)
    runtime_report=json.loads((a.kernel_build/'result.json').read_text())
    if runtime_report.get('actual_sensor_runtime_composition') is True:
        for archive_name,member,field in (('libEldenSensorMemory.a','MemoryBinding.o','changed_memory_binding_archive'),
                                         ('libEldenMotionSsc.a','NativeSsc.o','changed_SSC_archive'),
                                         ('libShengUfs.a','ShengUfs.o','changed_runtime_UFS_archive')):
            oldr=out/('runtime-old-'+member)/archive_name;newr=out/('runtime-new-'+member)/archive_name
            oldr.parent.mkdir();newr.parent.mkdir()
            for folder,obj in ((oldr.parent,out/'old.o'),(newr.parent,out/'new.o')):
                (folder/member).write_bytes(obj.read_bytes());(folder/'peer.o').write_bytes((out/'peer.o').read_bytes())
                run(['ar','rcs',folder/archive_name,member,'peer.o'],folder.name+'-archive',folder)
            old_members=members(oldr);new_members=members(newr);argumentr='-Wl,-force_load,'+str(oldr);cmdr=['-Wl,-force_load,'+str(newr)]
            proof={'inherited_inputs':{str(oldr):sha(oldr)},'actual_sensor_runtime_composition':True,
                   field:{n:{'before':old_members[n],'after':new_members[n]} for n in old_members}}
            verify('exact_runtime_member_'+member,lambda:retained_archive(argumentr,cmdr,proof))
            wrong=copy.deepcopy(proof);wrong.pop('actual_sensor_runtime_composition')
            verify('runtime_member_without_profile_'+member,lambda:retained_archive(argumentr,cmdr,wrong),True)
            wrong=copy.deepcopy(proof);wrong[field][member]['after']='f'*64
            verify('counterfeit_runtime_member_'+member,lambda:retained_archive(argumentr,cmdr,wrong),True)
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
    if runtime_report.get('actual_sensor_runtime_composition') is True:
        worker=symbol('_elden_sensor_runtime_worker');setup=symbol('_elden_sensor_runtime_prepare')
        runtime_calls=[(worker+4,symbol(n)) for n in ('_elden_sensor_adsp_endpoint_open','_elden_sensor_memory_binding_open_prepared','_elden_motion_qrtr_transport','_elden_motion_native_ssc_start')]
        runtime_calls.append((setup+4,symbol('_elden_qrtr_ports_prepare')))
        verify('exact_runtime_worker_and_setup_calls',lambda:no_outer_sensor_start(runtime_calls,symbol,True,native))
        verify('runtime_outer_startup_refused',lambda:no_outer_sensor_start(runtime_calls+[(worker+4,symbol('_elden_sensor_runtime_start'))],symbol,True,native),True)
        verify('unrelated_runtime_worker_call_refused',lambda:no_outer_sensor_start(runtime_calls+[(symbol('_elden_sensor_runtime_status')+4,symbol('_elden_sensor_adsp_endpoint_open'))],symbol,True,native),True)
        prepared=symbol('_elden_sensor_memory_binding_open_prepared')
        verify('actual_prepared_memory_binding_calls',lambda:explicit_binding_calls('_elden_sensor_pack_binding_open',[(prepared+4,target)],symbol,native,True))
        verify('prepared_binding_without_profile_refused',lambda:explicit_binding_calls('_elden_sensor_pack_binding_open',[(prepared+4,target)],symbol,native),True)
    result={'passed':True,'checks':checks,'actual_compiled_ar_members':True,'actual_native_function_symbols':True,'negative_calls_and_declarations_injected':True,
            'kernel_sha256':sha(a.kernel_build/'kernel.macho'),'native_sha256':sha(native),'tests':tests,'hardware':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'passed':True,'checks':checks,'negative_gates':True}))
if __name__=='__main__':main()
