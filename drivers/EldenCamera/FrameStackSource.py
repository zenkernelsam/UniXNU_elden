"""Extend actual guarded-DMA board test through retained frame snapshot output."""
from pathlib import Path
import hashlib
from DmaStackSource import derive as dma_stack
HERE=Path(__file__).resolve().parent
def derive():
    raw,prior=dma_stack();text=raw.decode();changes=[]
    def patch(old,new,why):
        nonlocal text
        if text.count(old)!=1:raise ValueError('Actual frame stack anchor differs: '+why)
        text=text.replace(old,new);changes.append(why)
    patch('#include "DmaBinding.h"','#include "DmaBinding.h"\n#include "FrameOutput.h"','actual retained frame-output API')
    patch('EldenCameraDmaCapture capture{};','EldenCameraFrameOutput *snapshots[2]={};EldenCameraDmaCapture capture{};','real output for each sensor')
    before='CHECK(!elden_camera_dma_capture_start(&capture,target)&&(elden_camera_i2c10_peer_stat(i2c_peer,6)&0x40)&&elden_camera_clock_peer_mclk(target)&&!elden_camera_clock_peer_reset(target));'
    patch(before,before+'\n        CHECK(!elden_camera_output_open(&capture,target,2,&snapshots[target]));','snapshot allocation only after actual sensor/receiver started')
    start=text.index('        CHECK(!elden_camera_dma_capture_service(&capture,target));EldenCameraFrame frame{};')
    end=text.index('        CHECK(!elden_camera_dma_capture_release(&capture,target,&frame,1));',start)+len('        CHECK(!elden_camera_dma_capture_release(&capture,target,&frame,1));')
    text=text[:start]+'''        CHECK(!elden_camera_output_pump(snapshots[target]));EldenCameraOutputFrame frame{};
        CHECK(!elden_camera_output_acquire(snapshots[target],&frame)&&frame.sensor==target&&frame.bytes==memory.slots[target?3:0].buffer.bytes);
        CHECK(elden_camera_dma_peer_completion_count()==target+1);
        CHECK(elden_camera_output_memory(snapshots[target],frame.ticket,frame.slot));
        CHECK(!elden_camera_output_release(snapshots[target],frame.ticket));
        CHECK(elden_camera_output_release(snapshots[target],frame.ticket)==-116);
''' +text[end:];changes.append('actual done→borrow→cachedsnapshot→realreturn/requeue→acquire/read-only/release')
    patch('CHECK(!elden_camera_dma_status(dma,&dma_status)&&dma_status.retired);',
          'CHECK(!elden_camera_dma_status(dma,&dma_status)&&dma_status.retired);\n    for(unsigned i=0;i<2;i++)CHECK(!elden_camera_output_stop(snapshots[i]));','output retirement after actual Board and SMMU/DMA retirement')
    text=text.replace('PASS actual guarded-DMA board Gen4 front/rear capture %s:','PASS actual frame-snapshot board Gen4 front/rear capture %s:')
    return text.encode(),{'parent_sha256':hashlib.sha256(raw).hexdigest(),'parent':prior,'changes':changes,'actual_snapshot_allocators_capture_chain':True,'hardware':False}
