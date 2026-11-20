"""Reject counterfeits at exact real archive-member and shared startup gates."""
from pathlib import Path
import argparse,copy,json,subprocess,sys
from peripheral_archive_audit import retained_archive
from peripheral_startup_audit import no_outer_sensor_start
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('kernel-build','prelink-tool','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    r=json.loads((a.kernel_build/'result.json').read_text());cmd=json.loads((a.kernel_build/'link-command.json').read_text());tests=[]
    def verify(name,fn,refuse=False):
        try:fn()
        except (ValueError,KeyError,FileNotFoundError) as error:
            if not refuse:raise
            tests.append({'case':name,'refused':True,'reason':str(error)})
        else:
            if refuse:raise ValueError('Counterfeit accepted: '+name)
            tests.append({'case':name,'accepted':True})
    for name,row in r['shared_archive_deltas'].items():
        arg='-Wl,-force_load,'+row['before_path'];verify('exact_'+name,lambda:retained_archive(arg,cmd,r))
        for key in ('original_sha256','replacement_sha256','before_path','after_path','changed','members'):
            wrong=copy.deepcopy(r);d=wrong['shared_archive_deltas'][name]
            if key.endswith('sha256'):d[key]='1'*64
            elif key.endswith('path'):d[key]+='.counterfeit'
            elif key=='changed':d[key]=[]
            else:d[key][row['changed'][0]]['after']='1'*64
            verify('counterfeit_'+name+'/'+key,lambda:retained_archive(arg,cmd,wrong),True)
        wrong=copy.deepcopy(r);wrong.pop('actual_shared_SMMU_native_staged_peripherals')
        verify('without_profile_'+name,lambda:retained_archive(arg,cmd,wrong),True)
        verify('duplicate_'+name,lambda:retained_archive(arg,cmd+['-Wl,-force_load,'+row['after_path']],r),True)
    sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports
    raw=(a.kernel_build/'kernel.macho').read_bytes();names={n:v for v,g in exports(raw,macho(raw)).items() for n in g};symbol=lambda n:names[n]
    worker=symbol('_elden_sensor_runtime_worker');setup=symbol('_elden_sensor_runtime_prepare');adopt=symbol('_elden_sensor_runtime_adopt')
    calls=[(worker+4,symbol(n)) for n in ('_elden_sensor_adsp_endpoint_open','_elden_sensor_memory_binding_open_prepared','_elden_motion_qrtr_transport','_elden_motion_native_ssc_start')]
    calls.extend([(setup+4,symbol('_elden_qrtr_ports_prepare')),(adopt+4,symbol('_elden_qrtr_ports_prepare'))])
    verify('original_runtime_and_explicit_adopt_bounds',lambda:no_outer_sensor_start(calls,symbol,True,a.kernel_build/'native.macho'))
    verify('unqualified_adopt_caller',lambda:no_outer_sensor_start(calls+[(worker+4,adopt)],symbol,True,a.kernel_build/'native.macho'),True)
    verify('unrelated_qrtr_setup_caller',lambda:no_outer_sensor_start(calls+[(symbol('_elden_sensor_runtime_status')+4,symbol('_elden_qrtr_ports_prepare'))],symbol,True,a.kernel_build/'native.macho'),True)
    report={'passed':True,'checks':len(tests),'actual_ar_members_and_native_functions':True,'injected_counterfeit_evidence':True,'tests':tests,'hardware':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS '+str(len(tests))+' actual shared archive/member/adopt startup audit checks')
if __name__=='__main__':main()
