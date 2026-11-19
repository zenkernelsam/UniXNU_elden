"""Prepare exact OS17 motion HID methods/descriptors, without a Gen4 producer."""
from pathlib import Path
import argparse,hashlib,json,shutil,struct,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('kernel-build','prelink-tool','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    report=json.loads((a.kernel_build/'result.json').read_text());raw=(a.kernel_build/'kernel.macho').read_bytes()
    if not report['passed'] or sha(a.kernel_build/'kernel.macho')!=report['kernel_sha256']:raise ValueError('Kernel identity differs')
    sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,backed_offset,exports
    im=macho(raw);names={n:v for v,g in exports(raw,im).items() for n in g};spec=json.loads((DRIVER/'hid-source.json').read_text())
    def read(va,n):
        off=backed_offset(im['segments'],va,n,len(raw));return raw[off:off+n]
    meta=json.loads((a.kernel_build/'kernel.json').read_text())
    extent=next(e for e in meta['extents'] if e['module']=='com.apple.iokit.IOHIDFamily' and e['segment']=='__TEXT_EXEC')
    if hashlib.sha256(read(extent['va'],extent['size'])).hexdigest()!=spec['original_HID_text_sha256']:raise ValueError('Original HID code differs')
    table=struct.unpack('<220Q',read(0xfffffff005d06a68,220*8))
    for key,row in spec['bound_original_OS17'].items():
        if hashlib.sha256(read(int(row['va'],16),row['bytes'])).hexdigest()!=row['sha256']:raise ValueError('Original motion constructor/method differs: '+key)
    if table[44]!=int(spec['bound_original_OS17']['set_properties']['va'],16) or table[201]!=int(spec['bound_original_OS17']['copy_event']['va'],16) or table[198]!=0xfffffff004aface8:
        raise ValueError('Original event-driver table slots differ')
    abi=['// Generated after exact OS17 original code/table qualification.','#pragma once','#include <stdint.h>',
         '#define ELDEN_MOTION_DRIVER_VPTR 0xfffffff005d06a68ULL','#define ELDEN_MOTION_DRIVER_DISPATCH 0xfffffff004aface8ULL',
         '#define ELDEN_MOTION_DRIVER_SET_PROPERTIES '+spec['bound_original_OS17']['set_properties']['va']+'ULL',
         '#define ELDEN_MOTION_ACCEL_EVENT '+spec['bound_original_OS17']['accel']['va']+'ULL',
         '#define ELDEN_MOTION_GYRO_EVENT '+spec['bound_original_OS17']['gyro']['va']+'ULL',
         'static const uintptr_t elden_motion_driver_table[220]={'+','.join(hex(v)+'ULL' for v in table)+'};']
    (out/'MotionOriginalAbi.h').write_text('\n'.join(abi)+'\n')
    shutil.copyfile(ROOT/'profiles/v91/native-build/extra/47901aa7a085/OriginalHidDeviceABI.h',out/'OriginalHidDeviceABI.h')
    sys.path.insert(0,str(DRIVER));from HidSource import descriptors
    (out/'MotionDescriptors.inc').write_bytes(descriptors())
    for n in ['NativeHid.h','NativeHidMethods.inc','Hid.c','Hid.h','Core.h','LICENSE.HID']:shutil.copyfile(DRIVER/n,out/n)
    shutil.copyfile(DRIVER/'NativeHid.cpp',out/'EldenMotionHid.cpp')
    (out/'source.json').write_text(json.dumps({'qualified':True,'reference_kernel_sha256':report['kernel_sha256'],
        'donor':spec,'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'constructor_full_bodies_matched':True,
        'original_event_driver_220_slots_matched':True,'actual_Gen4_producer_connected':False,'device_orientation_service':False,'hardware':False},indent=2)+'\n')
    print('PASS: original OS17 motion constructor bodies/driver220slots and source-bound accel/gyro descriptors; no producer')
if __name__=='__main__':main()
