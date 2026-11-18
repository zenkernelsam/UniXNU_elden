"""Prepare source-bound haptics/native and an exact pen-preserving touch hook.

Firmware is an external original input. Absent measured calibration produces
a disabled output catalog; no userspace switch can supply raw gains/voltages.
"""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenHaptics'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def integer(v,lo,hi):
    if type(v) is not int or not lo<=v<=hi:raise ValueError('Calibration integer/range invalid')
    return v
def calibration(path,firmware_sha):
    if path is None:
        return ('#pragma once\n#include "Queue.h"\n#define ELDEN_HAPTICS_HAS_LIMITS 0\n'
                '#define ELDEN_HAPTICS_MIN_TEMP_CENTI 0\n#define ELDEN_HAPTICS_MAX_TEMP_CENTI 0\n'
                'static const uint8_t elden_haptics_max_gain[2]={0,0};\n'
                'static const EldenHapticsQueueLimits elden_haptics_limits={};\n'),None
    r=read(path)
    if r.get('board')!='TB322FC' or r.get('chip_ids')!=[0x9370,0x9370] or r.get('firmware_sha256')!=firmware_sha or r.get('boost_mv')!=7000:
        raise ValueError('Calibration board/chip/firmware/boost differs')
    evidence=r.get('evidence',[])
    if not evidence or not r.get('review_notes'):raise ValueError('Measured calibration evidence/review notes required')
    qualified=[]
    for row in evidence:
        p=(path.parent/row['path']).resolve(strict=True)
        if sha(p)!=row['sha256']:raise ValueError('Calibration evidence changed')
        qualified.append({'bytes':p.stat().st_size,'sha256':row['sha256']})
    limits=r['limits'];points=r['points'];max_gain=[integer(v,1,128) for v in limits['max_gain']]
    if len(max_gain)!=2 or not 2<=len(points)<=64:raise ValueError('Two motors and bounded LUT required')
    normalized=[]
    for row in points:
        intensity=integer(row['intensity_q15'],0,32767);gains=row['gain']
        if len(gains)!=2:raise ValueError('Per-motor gains required')
        gains=[integer(v,0,max_gain[i]) for i,v in enumerate(gains)]
        if normalized and (intensity<=normalized[-1][0] or any(gains[i]<normalized[-1][1][i] for i in range(2))):raise ValueError('Nonmonotonic measured LUT')
        normalized.append((intensity,gains))
    if normalized[0]!=(0,[0,0]) or normalized[-1][0]!=32767:raise ValueError('LUT endpoints invalid')
    duration=integer(limits['max_duration_us'],1,250000);late=integer(limits['max_late_us'],1,1000000)
    window=integer(limits['window_us'],1,10000000);on=[integer(v,1,window) for v in limits['max_on_us']]
    if len(on)!=2:raise ValueError('Per-motor on-time budgets required')
    dual=integer(limits['max_dual_on_us'],1,window);sum_gain=integer(limits['max_sum_gain'],1,256)
    square=[integer(v,1,16384*window) for v in limits['square_budget']]
    if len(square)!=2 or any(sum(g)>sum_gain for _,g in normalized):raise ValueError('Combined/square budget invalid')
    low=integer(r['temperature_centi_c'][0],-1000,5000);high=integer(r['temperature_centi_c'][1],low,5000)
    text='#pragma once\n#include "Queue.h"\n#define ELDEN_HAPTICS_HAS_LIMITS 1\n'
    text+=f'#define ELDEN_HAPTICS_MIN_TEMP_CENTI {low}\n#define ELDEN_HAPTICS_MAX_TEMP_CENTI {high}\n'
    text+='static const uint8_t elden_haptics_max_gain[2]={'+','.join(map(str,max_gain))+'};\n'
    text+='static const EldenHapticsClickPoint elden_haptics_points[]={'+','.join('{'+str(i)+',{'+','.join(map(str,g))+'}}' for i,g in normalized)+'};\n'
    text+='static const EldenHapticsQueueLimits elden_haptics_limits={elden_haptics_points,'
    text+=','.join(map(str,[duration,late,window]))+',{'+','.join(map(str,on))+'},'+str(dual)+',{'+','.join(str(v)+'ULL' for v in square)+'},'
    text+=str(sum_gain)+','+str(len(normalized))+',{'+','.join(map(str,max_gain))+'},3};\n'
    return text,{'input_sha256':sha(path),'evidence':qualified,'review_notes':r['review_notes'],'hardware_measured_by_this_tool':False}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('firmware','pen-source','pen-build','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--calibration',type=Path);a=p.parse_args()
    for key in ('firmware','pen_source','pen_build'):setattr(a,key,getattr(a,key).resolve(strict=True))
    if a.calibration:a.calibration=a.calibration.resolve(strict=True)
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);h=out/'haptics';h.mkdir();touch=out/'touch';touch.mkdir()
    spec=read(DRIVER/'source.json');fw=spec['firmware']
    if a.firmware.stat().st_size!=fw['bytes'] or sha(a.firmware)!=fw['sha256']:raise ValueError('Exact Gen4 firmware required')
    sys.path.insert(0,str(DRIVER));from AwSource import derive;from GeniSource import sources
    (h/'AwChip.c').write_bytes(derive())
    for name,data in sources().items():(h/name).write_bytes(data)
    for name in ('AwChip.h','AwKernelShim.h','AwRtp.h','Bus.c','Bus.h','Board.h','Platform.c','Platform.h','Queue.c','Queue.h','Mailbox.c','Mailbox.h','RtpStream.c','RtpStream.h','Native.h'):
        shutil.copyfile(DRIVER/name,h/name)
    shutil.copyfile(DRIVER/'Native.cpp',h/'EldenHapticsNative.cpp')
    data=a.firmware.read_bytes();(h/'EldenHapticsFirmware.h').write_text('#pragma once\nstatic const unsigned char elden_haptics_firmware[]={'+','.join(hex(v) for v in data)+'};\n')
    text,cal=calibration(a.calibration,fw['sha256']);(h/'EldenHapticsCalibration.h').write_text(text)
    pen=read(a.pen_source/'source.json');built=read(a.pen_build/'result.json')
    if not built['compiled']:raise ValueError('Pen/touch parent incomplete')
    for name,digest in pen['sources'].items():
        if sha(a.pen_source/name)!=digest:raise ValueError('Parent pen source changed')
        shutil.copyfile(a.pen_source/name,touch/name)
    runtime=touch/'EldenTouchRuntime.cpp';body=runtime.read_text()
    expected=built['objects']['EldenTouchRuntime.cpp']['source_sha256']
    if sha(runtime)!=expected:raise ValueError('Actual built pen/touch source differs')
    anchor='extern "C" void elden_touch_runtime_start(void){'
    if body.count(anchor)!=1:raise ValueError('Unique touch startup entry required')
    body='extern "C" void elden_haptics_native_start(void);\n'+body.replace(anchor,anchor+'\n    elden_haptics_native_start(); // independent default-off worker')
    runtime.write_text(body)
    rows={str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file()}
    (out/'source.json').write_text(json.dumps({'sources':rows,'upstream':spec,'firmware_sha256':fw['sha256'],
        'pen_source_record_sha256':sha(a.pen_source/'source.json'),'parent_touch_source_sha256':expected,
        'calibration':cal,'public_raw_register_or_gain':False,'hardware':False},indent=2)+'\n')
    print('Prepared actual haptics/native/firmware and pen-preserving touch entry; calibration '+('external reviewed input' if cal else 'absent/output disabled'))
if __name__=='__main__':main()
