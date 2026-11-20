"""Exact actual compiled camera wrapper boundaries; no implicit startup allow."""
import re,subprocess
def check_camera_startup(calls,symbol,native):
    starts=[]
    for line in subprocess.check_output(['xcrun','nm','-n',str(native)],text=True).splitlines():
        m=re.match(r'^([0-9a-fA-F]+)\s+[sStT]\s+(\S+)$',line)
        if m:starts.append((int(m[1],16),m[2]))
    for name in ('_elden_camera_native_cci_open','_elden_camera_native_pipeline_open',
                 '_elden_camera_capture_bind','_elden_camera_capture_probe','_elden_camera_capture_start'):
        if any(t==symbol(name) for pc,t in calls):raise ValueError('Unqualified outer camera startup caller: '+name)
    proof={}
    for callee,caller in (('_elden_camera_sensor_bind','_elden_camera_capture_bind'),
                          ('_elden_camera_sensor_probe','_elden_camera_capture_probe'),
                          ('_elden_camera_sensor_start','_elden_camera_capture_start')):
        start=symbol(caller);end=min((pc for pc,_ in starts if pc>start),default=None)
        hits=[pc for pc,t in calls if t==symbol(callee)]
        if end is None or not hits or any(not start<=pc<end for pc in hits):raise ValueError('Incoming camera call outside actual capture wrapper: '+callee)
        proof[callee]={'caller':caller,'function_start':hex(start),'next_function':hex(end),'calls_and_tails':[hex(pc) for pc in hits]}
    return proof
