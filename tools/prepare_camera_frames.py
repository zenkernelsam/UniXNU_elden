"""Prepare actual camera snapshots/service/RAW10 converter; no device/startup."""
from pathlib import Path
import argparse,hashlib,json,math,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];CAM=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(CAM));from DmaSource import derive
    # Retain exact current original DMA allocator dependencies, no substitute.
    for name,data in derive()[0].items():(out/name).write_bytes(data)
    names=('FrameOutput.h','FrameOutput.cpp','FrameOutputMethods.inc','Service.h','Service.cpp','ServiceMethods.inc','ServiceAbi.h','Raw10.h','Raw10.c',
           'DmaBinding.h','NativeDma.h','Board.h','Pipeline.h','Core.h','NativeClocks.h','Clock.h','PowerBinding.h','Power.h','SharedI2c10.h','NativePipeline.h','SensorBinding.h','NativeCci.h','Cci.h')
    for n in names:shutil.copyfile(CAM/n,out/n)
    for n in ('CameraClient.h','CameraClient.c','CameraProvider.m'):shutil.copyfile(CAM/'ios'/n,out/n)
    client=(out/'CameraClient.c').read_text();client=client.replace('#include <IOKit/IOKitLib.h>','#ifndef ELDEN_CAMERA_CLIENT_TEST\n#include <IOKit/IOKitLib.h>\n#endif')
    client=client.replace('#include <mach/mach.h>','#ifndef ELDEN_CAMERA_CLIENT_TEST\n#include <mach/mach.h>\n#endif');(out/'CameraClientMethods.inc').write_text(client,newline='\n')
    for n in ('frames-source.json','LICENSE.CMIO'):shutil.copyfile(CAM/n,out/n)
    header=(out/'NativeDma.h').read_text().replace('"../EldenSmmu/upstream/EldenWifiSmmu4KTableMemory.h"','"EldenWifiSmmu4KTableMemory.h"');(out/'NativeDma.h').write_text(header,newline='\n')
    up=ROOT/'drivers/EldenSmmu/upstream';record=json.loads((up/'source.json').read_text())
    for row in record['files'].values():
        src=up/row['output']
        if sha(src)!=row['output_sha256']:raise ValueError('Frozen original4K source changed')
        if src.suffix in ('.c','.h') or src.name=='LICENSE':shutil.copyfile(src,out/src.name)
    from ClockSource import derive as clocks
    (out/'ClockIds.h').write_bytes(clocks()[0]['ClockIds.h'])
    lut=[]
    for i in range(256):
        x=i/255;s=12.92*x if x<=0.0031308 else 1.055*math.pow(x,1/2.4)-0.055;lut.append(min(255,max(0,int(s*255+0.5))))
    (out/'Raw10Srgb.inc').write_text('/* SPDX-License-Identifier: BSD-2-Clause-Patent */\n/* IEC61966-2-1 transfer; generated software preview, not sensor calibration. */\nstatic const unsigned char elden_raw10_srgb[256]={'+','.join(map(str,lut))+'};\n',newline='\n')
    result={'sources':{f.name:sha(f) for f in out.iterdir() if f.is_file()},'parent_table_manifest_sha256':sha(up/'source.json'),
            'actual_camera_done_borrow_snapshot_return_service':True,'pixel_generator':False,'hardware':False,'automatic_startup':False}
    (out/'source.json').write_text(json.dumps(result,indent=2)+'\n',newline='\n');print('Prepared original DMA capture -> retained read-only snapshots, real OS17 service and full10bit preview')
if __name__=='__main__':main()
