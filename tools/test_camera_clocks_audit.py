"""Inject misplaced/automatic camera board calls against actual function bounds."""
from pathlib import Path
import argparse,json,subprocess,sys
from camera_startup_audit import check_camera_startup
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('kernel-build','prelink-tool','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports
    raw=(a.kernel_build/'kernel.macho').read_bytes();names={n:v for v,g in exports(raw,macho(raw)).items() for n in g};symbol=lambda n:names[n]
    pairs=[('_elden_camera_sensor_bind','_elden_camera_capture_bind'),('_elden_camera_sensor_probe','_elden_camera_capture_probe'),
        ('_elden_camera_sensor_start','_elden_camera_capture_start'),('_elden_camera_capture_bind','_elden_camera_powered_capture_bind'),
        ('_elden_camera_capture_probe','_elden_camera_powered_capture_probe'),('_elden_camera_capture_start','_elden_camera_powered_capture_start'),
        ('_elden_camera_native_cci_open','_elden_camera_board_prepare'),('_elden_camera_native_pipeline_open','_elden_camera_board_prepare'),
        ('_elden_camera_powered_capture_bind','_elden_camera_board_prepare'),('_elden_camera_powered_capture_probe','_elden_camera_board_probe'),
        ('_elden_camera_powered_capture_start','_elden_camera_board_start')]
    calls=[(symbol(caller)+4,symbol(callee)) for callee,caller in pairs];tests=[];native=a.kernel_build/'native.macho'
    def test(name,edges,refuse=False):
        try:check_camera_startup(edges,symbol,native,True,True)
        except ValueError as e:
            if not refuse:raise
            tests.append({'case':name,'refused':True,'reason':str(e)})
        else:
            if refuse:raise ValueError('Misplaced startup accepted: '+name)
            tests.append({'case':name,'accepted':True})
    test('all_actual_source_boundaries',calls)
    for callee,caller in pairs:
        wrong=[p for p in calls if p[1]!=symbol(callee)]+[(symbol(caller)-4,symbol(callee))]
        test('before_actual_function_'+callee,wrong,True)
    for name in ('_elden_camera_board_bind','_elden_camera_board_prepare','_elden_camera_board_probe','_elden_camera_board_start','_elden_camera_native_clocks_open'):
        test('unqualified_outer_'+name,calls+[(symbol('_elden_camera_clocks_status')+4,symbol(name))],True)
    (out/'result.json').write_text(json.dumps({'passed':True,'checks':len(tests),'actual_function_bounds':True,'injected_negative_edges':True,'tests':tests,'hardware':False},indent=2)+'\n')
    print('PASS actual camera clock/board startup rejection gates: '+str(len(tests))+' checks')
if __name__=='__main__':main()
