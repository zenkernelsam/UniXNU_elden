"""Test compiled DMA/board/capture startup boundaries against misplaced calls."""
from pathlib import Path
import argparse,json,sys
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
    pairs += [('_elden_camera_board_'+suffix,'_elden_camera_dma_capture_'+suffix) for suffix in ('bind','prepare','probe','start')]
    calls=[(symbol(caller)+4,symbol(callee)) for callee,caller in pairs];native=a.kernel_build/'native.macho';tests=[]
    def test(name,edges,refuse=False,dma=True):
        try:check_camera_startup(edges,symbol,native,True,True,dma)
        except ValueError as e:
            if not refuse:raise
            tests.append({'case':name,'refused':True,'reason':str(e)})
        else:
            if refuse:raise ValueError('Unqualified startup accepted: '+name)
            tests.append({'case':name,'accepted':True})
    test('all_actual_DMA_board_capture_functions',calls)
    test('new_DMA_calls_without_exact_profile',calls,True,False)
    for callee,caller in pairs:
        test('before_actual_'+callee,[x for x in calls if x[1]!=symbol(callee)]+[(symbol(caller)-4,symbol(callee))],True)
    for name in ('_elden_camera_dma_open','_elden_camera_dma_capture_bind','_elden_camera_dma_capture_prepare','_elden_camera_dma_capture_probe',
                 '_elden_camera_dma_capture_queue_initial','_elden_camera_dma_capture_start','_elden_camera_native_clocks_open'):
        test('unqualified_outer_'+name,calls+[(symbol('_elden_camera_dma_status')+4,symbol(name))],True)
    (out/'result.json').write_text(json.dumps({'passed':True,'checks':len(tests),'actual_function_bounds':True,'injected_negative_calls':True,'tests':tests,'hardware':False},indent=2)+'\n')
    print('PASS actual native camera DMA/board startup refusal gates: '+str(len(tests))+' checks')
if __name__=='__main__':main()
