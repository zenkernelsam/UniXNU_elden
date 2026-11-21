"""Extend actual guarded-DMA board test through retained frame snapshot output."""
from pathlib import Path
import hashlib
from DmaStackSource import derive as dma_stack
HERE=Path(__file__).resolve().parent
def derive(irq=False):
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
    if irq:
        patch('#include "NativePipelineMethods.inc"', '''#include "NativePipelineMethods.inc"
static void camera_frame_irq(unsigned sensor){
    uint32_t id=sensor?637u:633u,word=id/32*4,bit=1u<<(id%32);bool found=false;
    CHECK(gic[(0x100+word)/4]&bit);
    for(auto handler:handlers)if(handler.id==id){
        found=true;gic[(0x300+word)/4]|=bit;capture_irq_mode=true;
        handler.callback(handler.context);capture_irq_mode=false;gic[(0x300+word)/4]&=~bit;
    }CHECK(found&&!(gic[(0x100+word)/4]&bit));
}''', 'actual registered primary CSID callback with explicit modeled GIC edge/EOI')
        patch('EldenCameraFrameOutput *snapshots[2]={};',
              'uint64_t retained_tickets[2]={};EldenCameraFrameOutput *snapshots[2]={};',
              'held consumer tickets remain through real camera shutdown')
        patch('CHECK(!elden_camera_output_open(&capture,target,2,&snapshots[target]));',
              '''CHECK(!elden_camera_output_open(&capture,target,2,&snapshots[target]));
        CHECK(!elden_camera_output_enable_irq(snapshots[target]));
        CHECK(elden_camera_output_pump_irq(snapshots[target],20)==-11);
        CHECK(camera_test_refs==target+1);''', 'explicit current original worker listen then first real status service, no generated frame')
        patch('elden_camera_pipeline_peer_event(capture_peers[target],1,1,100,110);',
              '''unsigned completion_base=elden_camera_dma_peer_completion_count();
        elden_camera_pipeline_peer_event(capture_peers[target],1,1,100,110);camera_frame_irq(target);''',
              'actual source event delivered through retained IRQ before snapshot wait/service')
        patch('CHECK(!elden_camera_output_pump(snapshots[target]));EldenCameraOutputFrame frame{};',
              'CHECK(!elden_camera_output_pump_irq(snapshots[target],20));EldenCameraOutputFrame frame{};',
              'original wait→CSID ack→real visibility/borrow/copy/return/requeue')
        patch('CHECK(elden_camera_dma_peer_completion_count()==target+1);',
              'CHECK(elden_camera_dma_peer_completion_count()==completion_base+1);',
              'actual source completion count delta, not a frame generator')
        patch('CHECK(elden_camera_output_release(snapshots[target],frame.ticket)==-116);',
              '''CHECK(elden_camera_output_release(snapshots[target],frame.ticket)==-116);
        EldenCameraOutputFrame held[2]={};
        for(unsigned number=1;number<=3;number++){
            elden_camera_pipeline_peer_event(capture_peers[target],1,1,100+100*number,110+100*number);
            camera_frame_irq(target);int pumped=elden_camera_output_pump_irq(snapshots[target],20);
            CHECK(pumped==(number==3?-11:0));
            if(number<3){CHECK(!elden_camera_output_acquire(snapshots[target],&held[number-1]));
                CHECK(held[number-1].sequence==number&&held[number-1].sensor==target);}
        }
        CHECK(elden_camera_dma_peer_completion_count()==completion_base+4);
        EldenCameraOutputStatus output_status{};CHECK(!elden_camera_output_status(snapshots[target],&output_status));
        CHECK(output_status.held==2&&output_status.dropped==1);
        CHECK(!elden_camera_output_release(snapshots[target],held[1].ticket));
        retained_tickets[target]=held[0].ticket;
        CHECK(elden_camera_output_pump_irq(snapshots[target],20)==-11);''',
              'four real source events, both held snapshots refuse overwrite, actual input still returned/requeued')
        patch('for(unsigned i=0;i<2;i++)CHECK(!elden_camera_output_stop(snapshots[i]));',
              '''for(unsigned i=0;i<2;i++){
        CHECK(!elden_camera_output_stop(snapshots[i]));
        EldenCameraOutputStatus status{};CHECK(!elden_camera_output_status(snapshots[i],&status)&&status.retired);
        if(retained_tickets[i]){CHECK(status.held==1);CHECK(!elden_camera_output_release(snapshots[i],retained_tickets[i]));}
        CHECK(elden_camera_output_pump_irq(snapshots[i],20)<0);
    }''', 'real stop wakes original worker; held ticket returned explicitly after device retirement')
        text=text.replace('PASS actual frame-snapshot board Gen4 front/rear capture %s:',
                          'PASS actual IRQ→originalworker→DMA→snapshot Gen4 front/rear %s:')
    text=text.replace('PASS actual guarded-DMA board Gen4 front/rear capture %s:','PASS actual frame-snapshot board Gen4 front/rear capture %s:')
    return text.encode(),{'parent_sha256':hashlib.sha256(raw).hexdigest(),'parent':prior,'changes':changes,'actual_snapshot_allocators_capture_chain':True,'hardware':False}
