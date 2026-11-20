"""Test actual camera function-boundary and native-input identity rejection gates."""
from pathlib import Path
import argparse,copy,hashlib,json,subprocess,sys
from camera_startup_audit import check_camera_startup
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('kernel-build','prelink-tool','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports
    raw=(a.kernel_build/'kernel.macho').read_bytes();names={n:v for v,g in exports(raw,macho(raw)).items() for n in g}
    def symbol(n):return names[n]
    native=a.kernel_build/'native.macho';calls=[]
    for callee,caller in (('_elden_camera_sensor_bind','_elden_camera_capture_bind'),('_elden_camera_sensor_probe','_elden_camera_capture_probe'),('_elden_camera_sensor_start','_elden_camera_capture_start')):
        calls.append((symbol(caller)+4,symbol(callee)))
    tests=[]
    def verify(name,fn,refused=False):
        try:fn()
        except ValueError as e:
            if not refused:raise
            tests.append({'case':name,'refused':True,'reason':str(e)})
        else:
            if refused:raise ValueError('Incorrectly accepted injected negative: '+name)
            tests.append({'case':name,'accepted':True})
    verify('actual_compiled_capture_wrapper_boundaries',lambda:check_camera_startup(calls,symbol,native))
    for name in ('_elden_camera_native_cci_open','_elden_camera_native_pipeline_open','_elden_camera_capture_bind','_elden_camera_capture_probe','_elden_camera_capture_start'):
        verify('refuse_automatic_'+name,lambda name=name:check_camera_startup(calls+[(symbol('_elden_camera_pipeline_open')+4,symbol(name))],symbol,native),True)
    for callee,caller in (('_elden_camera_sensor_bind','_elden_camera_capture_bind'),('_elden_camera_sensor_probe','_elden_camera_capture_probe'),('_elden_camera_sensor_start','_elden_camera_capture_start')):
        wrong=[pair for pair in calls if pair[1]!=symbol(callee)]+[(symbol(caller)-4,symbol(callee))]
        verify('before_actual_wrapper_'+callee,lambda wrong=wrong:check_camera_startup(wrong,symbol,native),True)
    verify('missing_actual_sensor_start_edge',lambda:check_camera_startup(calls[:-1],symbol,native),True)
    result={'passed':True,'checks':len(tests),'tests':tests,'actual_compiled_function_bounds':True,'injected_calls_not_hardware':True,
        'kernel_sha256':sha(a.kernel_build/'kernel.macho'),'native_sha256':sha(native),'hardware':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS camera actual-function startup guards: '+str(len(tests))+' checks')
if __name__=='__main__':main()
