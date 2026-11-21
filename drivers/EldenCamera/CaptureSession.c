/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "CaptureSession.h"
static int good(EldenCameraCaptureSession *s,unsigned sensor){return s&&s->initialized&&s->capture->initialized&&sensor<2;}
int elden_camera_capture_session_bind(EldenCameraCaptureSession *s,EldenCameraDmaCapture *capture){
    if(!s||s->initialized||!capture||!capture->initialized)return -22;
    s->capture=capture;s->owner=capture->owner;s->epoch=capture->epoch;s->route_epoch=capture->route_epoch;s->initialized=1;return 0;
}
int elden_camera_capture_session_begin(EldenCameraCaptureSession *s,unsigned sensor,const EldenCameraPipelineConfig *config,const EldenCameraBoardClockPlan *plan,unsigned buffers){
    if(!good(s,sensor)||!config||!plan)return -22;
    if(s->error)return s->error;
    if(s->stopping||s->closed[sensor])return -19;if(s->stopped[sensor])return -125;if(s->opened[sensor])return -16;
    if(config->sensor!=sensor||config->owner!=s->owner||config->epoch!=s->epoch||config->route_epoch!=s->route_epoch)return -116;
    int rc=elden_camera_dma_capture_prepare(s->capture,config,plan);
    if(!rc){s->prepared[sensor]=1;rc=elden_camera_dma_capture_probe(s->capture,sensor);}
    if(!rc){s->probed[sensor]=1;rc=elden_camera_dma_capture_queue_initial(s->capture,sensor);}
    if(!rc){s->queued[sensor]=1;rc=elden_camera_dma_capture_start(s->capture,sensor);}
    if(!rc){s->started[sensor]=1;rc=elden_camera_output_open(s->capture,sensor,buffers,&s->output[sensor]);}
    if(!rc){s->opened[sensor]=1;rc=elden_camera_output_enable_irq(s->output[sensor]);}
    if(rc)return s->error=rc<0?rc:-13;
    s->listening[sensor]=1;return 0;
}
int elden_camera_capture_session_publish(EldenCameraCaptureSession *s,unsigned sensor){
    if(!good(s,sensor))return -22;
    if(s->error)return s->error;
    if(!s->opened[sensor]||s->closed[sensor]||s->stopping)return -19;
    if(s->published[sensor])return -16;
    int rc=elden_camera_service_publish(s->output[sensor],sensor);if(rc)return rc;
    s->published[sensor]=1;return 0;
}
int elden_camera_capture_session_serve(EldenCameraCaptureSession *s,unsigned sensor,unsigned timeout_ms){
    if(!good(s,sensor))return -22;
    if(!s->listening[sensor]||s->closed[sensor]||s->stopping)return -19;
    return elden_camera_output_pump_irq(s->output[sensor],timeout_ms);
}
int elden_camera_capture_session_stop(EldenCameraCaptureSession *s,unsigned sensor){
    if(!good(s,sensor))return -22;
    /* output[sensor] may be a retained non-null partial object from a failed
     * open (e.g. IOLock allocation): it never admitted a consumer, so its
     * close refusal must not block the physical teardown. */
    if(s->output[sensor]&&!s->closed[sensor]){int rc=elden_camera_output_close(s->output[sensor]);
        if(rc&&s->opened[sensor])return rc;s->closed[sensor]=1;}
    if(!s->stopped[sensor]){int rc=elden_camera_dma_capture_stop(s->capture,sensor);if(rc)return rc;s->stopped[sensor]=1;}
    return 0;
}
int elden_camera_capture_session_shutdown(EldenCameraCaptureSession *s){
    if(!s||!s->initialized)return -22;
    if(s->shutdown_done)return 0;s->stopping=1;int first=0;
    /* Admission closes for every opened output before any physical shutdown.
     * If a fully opened output refuses its close, physical teardown does not
     * start while its consumer path can still be live. A lockless partial
     * output never admitted a consumer: its close refusal is recorded as
     * logically closed and never blocks cleanup of either sensor. */
    for(unsigned sensor=0;sensor<2;sensor++)if(s->output[sensor]&&!s->closed[sensor]){
        int rc=elden_camera_output_close(s->output[sensor]);
        if(rc&&s->opened[sensor]){if(!first)first=rc;continue;}s->closed[sensor]=1;
    }
    if(first)return first;
    /* Both drains are attempted even when the first fails; the first real
     * error is preserved for the caller while the peer still stops. */
    for(unsigned sensor=0;sensor<2;sensor++)if(!s->stopped[sensor]){
        int rc=elden_camera_dma_capture_stop(s->capture,sensor);if(!rc)s->stopped[sensor]=1;else if(!first)first=rc;
    }
    if(first)return first;
    int rc=elden_camera_dma_capture_shutdown(s->capture);if(rc)return rc;
    /* Retire each opened output. A published slot is withdrawn only when the
     * registry holds exactly this session's output, which also covers a
     * partially published service whose CAS committed before its start
     * failed. A foreign registration is never touched through the sensor-
     * only lookup; our own unregistered output is still stopped and the
     * anomaly is reported honestly. */
    for(unsigned sensor=0;sensor<2;sensor++){
        if(!s->opened[sensor]||s->retired[sensor])continue;
        EldenCameraFrameOutput *reg=elden_camera_service_output(sensor);
        rc=reg==s->output[sensor]?elden_camera_service_withdraw_output(sensor,s->output[sensor])
            :elden_camera_output_stop(s->output[sensor]);
        if(rc){if(!first)first=rc;continue;}
        s->retired[sensor]=1;
        if(reg&&reg!=s->output[sensor]&&!first)first=-16;
    }
    if(first)return first;
    s->shutdown_done=1;return 0;
}
