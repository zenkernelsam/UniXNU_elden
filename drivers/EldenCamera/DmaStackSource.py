"""Execute actual camera DMA binding inside the original board capture test."""
from pathlib import Path
import hashlib
from BoardStackSource import derive as board_stack
HERE=Path(__file__).resolve().parent
def derive():
    raw,prior=board_stack();text=raw.decode();changes=[]
    def patch(before,after,why):
        nonlocal text
        if text.count(before)!=1:raise ValueError('Actual board-stack anchor changed: '+why)
        text=text.replace(before,after);changes.append(why)
    patch('#include "Board.h"','#include "Board.h"\n#include "DmaBinding.h"','actual retained DMA→camera board')
    patch('void elden_camera_clock_peer_setup(void);',
          'EldenCameraNativeDma *elden_camera_dma_peer_open(void);void elden_camera_dma_peer_fault(unsigned);unsigned elden_camera_dma_peer_completion_count(void);void elden_camera_dma_peer_reset(void);\nvoid elden_camera_clock_peer_setup(void);',
          'actual native allocator methods and original4K table peers')
    patch('EldenCameraBoard camera_board{};', 'EldenCameraDmaCapture capture{};EldenCameraNativeDma *dma=elden_camera_dma_peer_open();EldenCameraDmaView memory{};CHECK(!elden_camera_dma_view(dma,7,9,&memory));',
          'actual highPA→lowIOVA guarded frame memory')
    patch('CHECK(!elden_camera_board_bind(&camera_board,native_clock,&shared_power,&board_owner));',
          'CHECK(!elden_camera_dma_capture_bind(&capture,dma,native_clock,&shared_power,&board_owner));','real wrapper supplies actual arena visibility')
    text=text.replace('camera_board.attempted','capture.board.attempted').replace('&camera_board.captures','&capture.board.captures').replace('camera_board.captures','capture.board.captures')
    for before,after in [('elden_camera_board_prepare(&camera_board','elden_camera_dma_capture_prepare(&capture'),
        ('elden_camera_board_probe(&camera_board','elden_camera_dma_capture_probe(&capture'),('elden_camera_board_start(&camera_board','elden_camera_dma_capture_start(&capture'),
        ('elden_camera_board_stop(&camera_board','elden_camera_dma_capture_stop(&capture'),('elden_camera_board_shutdown(&camera_board)','elden_camera_dma_capture_shutdown(&capture)')]:
        if before not in text:raise ValueError('Missing actual board wrapper: '+before)
        text=text.replace(before,after)
    before='for(unsigned i=0;i<3;i++){EldenCameraBuffer b{(uint64_t)i+1,7,9,11,UINT64_C(0x20000000)+target*0x8000000u+i*0x2000000u,status.frame_bytes,(void *)(uintptr_t)(i+1)};CHECK(!elden_camera_native_pipeline_queue(7,9,target,&b));}'
    patch(before,'CHECK(!elden_camera_dma_capture_queue_initial(&capture,target));','real queued buffers replace synthetic mapping handles')
    patch('CHECK(!elden_camera_native_pipeline_service(7,9,target));EldenCameraFrame frame{};CHECK(!elden_camera_native_pipeline_frame(7,9,target,&frame)&&frame.buffer.id==1&&!frame.error);',
          '''CHECK(!elden_camera_dma_capture_service(&capture,target));EldenCameraFrame frame{};
        CHECK(!elden_camera_dma_capture_frame(&capture,target,&frame)&&frame.buffer.id==(target?4u:1u)&&!frame.error);
        CHECK(elden_camera_dma_peer_completion_count()==target+1);
        CHECK(elden_camera_native_pipeline_queue(7,9,target,&frame.buffer)==-16); // CPU still borrowing
        auto stale=frame;stale.timestamp_ns++;CHECK(elden_camera_dma_capture_release(&capture,target,&stale,1)==-116);
        CHECK(!elden_camera_dma_capture_release(&capture,target,&frame,1));''','actual SOF/done→visibility→borrow/explicitrelease→requeue chain')
    patch('CHECK(!elden_camera_dma_capture_shutdown(&capture));',
          '''elden_camera_dma_peer_fault(2);CHECK(elden_camera_dma_capture_shutdown(&capture)==-16);
    elden_camera_dma_peer_fault(0);CHECK(!elden_camera_dma_capture_shutdown(&capture));
    EldenCameraDmaStatus dma_status{};CHECK(!elden_camera_dma_status(dma,&dma_status)&&dma_status.retired);
    CHECK(!elden_camera_dma_capture_shutdown(&capture));''','failed actual route retirement retains all refs then one verified retry')
    patch('std::free(native_clock);std::free(shared_power.bus.port);',
          'elden_camera_dma_peer_reset();std::free(native_clock);std::free(shared_power.bus.port);','HOSTONLY native memory peers finally retired')
    patch('PASS actual board-clock Gen4 front/rear capture %s:', 'PASS actual guarded-DMA board Gen4 front/rear capture %s:', 'precise completion test label')
    patch('drain_fail=std::strcmp(argv[1],"drain-fail")==0;',
          'drain_fail=std::strcmp(argv[1],"drain-fail")==0;bool visibility_fail=std::strcmp(argv[1],"visibility-fail")==0;',
          'real visibility refusal closes normal work but preserves cleanup')
    patch('CHECK(elden_camera_cci_peer_stat(cci_peers[target],2));elden_camera_pipeline_peer_event(capture_peers[target],1,1,100,110);',
          '''CHECK(elden_camera_cci_peer_stat(cci_peers[target],2));
        elden_camera_pipeline_peer_event(capture_peers[target],1,1,100,110);
        if(visibility_fail){elden_camera_dma_peer_fault(1);CHECK(elden_camera_dma_capture_service(&capture,target)==-71);
            elden_camera_dma_peer_fault(0);continue;}''','actual SOF completion visibility error delivered by original pipeline')
    patch('        CHECK(elden_camera_cci_peer_stat(cci_peers[target],2));\n        elden_camera_pipeline_peer_event',
          '    }\n    for(unsigned target=0;target<2;target++){\n        if(visibility_fail&&target)continue;\n        CHECK(elden_camera_cci_peer_stat(cci_peers[target],2));\n        elden_camera_pipeline_peer_event',
          'start BOTH physical sensors before visibility fault closes new startup')
    return text.encode(),{'prior_board_test_sha256':hashlib.sha256(raw).hexdigest(),'prior':prior,'changes':changes,
        'actual_native_guarded_DMA_board_CCI_CAMSS_methods':True,'physical_MMIO_DMA_IOKit_PAR_PMIC_GIC_models':True,'hardware':False}
