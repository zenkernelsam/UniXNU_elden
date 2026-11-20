/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual source-derived CSIPHY/CSID/VFE register and two-buffer queue methods.
 * This adapter supplies owned access/metadata and propagates every refusal;
 * no Linux hardware/provider success emulation or generated frame contents. */
#include "Pipeline.h"
#include <stdbool.h>
static void pipe_zero(void *p,size_t bytes){uint8_t *b=p;for(size_t i=0;i<bytes;i++)b[i]=0;}
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef int32_t s32;typedef int64_t s64;
#define BIT(n) (UINT32_C(1)<<(n))
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define CAMSS_INIT_BUF_COUNT 2
#define CSID_RESET_TIMEOUT_MS 500
#define LANE_CFG_BITWIDTH 4
#define MEDIA_BUS_FMT_SGRBG10_1X10 0x300a
#define MEDIA_BUS_FMT_SRGGB10_1X10 0x300f
#define MSM_CSID_PAD_FIRST_SRC 1
#define V4L2_EVENT_FRAME_SYNC 1
#define EINVAL 22
#define ETIMEDOUT 110
#define IRQ_HANDLED 1
typedef int irqreturn_t;
#include "CamssVersion.inc"
#include "CsidDecode.inc"
struct pipeline;
struct completion {struct pipeline *core;uint32_t done;};
struct csiphy_lane_regs;
struct csiphy_device_regs {uint32_t offset,common_status_offset;const struct csiphy_lane_regs *lane_regs;int lane_array_size;};
struct csiphy_lanes_cfg {int num_data;struct {unsigned pos;} data[4];};
struct csi2_config {struct csiphy_lanes_cfg lane_cfg;};
struct csiphy_config {struct csi2_config *csi2;};
struct camss_resources {unsigned version;};
struct camss {struct pipeline *core;struct camss_resources *res;void *dev;};
struct csiphy_device {struct camss *camss;struct csiphy_device_regs *regs;uint32_t timer_clk_rate;};
struct csid_phy_config {u8 csiphy_id,lane_cnt;u32 lane_assign,en_vc;};
struct v4l2_mbus_framefmt {u32 code;};
struct csid_format_info {u32 code;u8 data_type,decode_format,bpp,spp;};
struct csid_formats {const struct csid_format_info *formats;unsigned nformats;};
struct csid_resources {struct csid_formats *formats;};
struct csid_device {struct camss *camss;struct csid_phy_config phy;u8 id;bool lite;uint32_t reg_update,aup_update,rup_update;
    struct completion reset_complete;struct v4l2_mbus_framefmt fmt[6];struct csid_resources *res;};
enum vfe_line_id {VFE_LINE_NONE=-1,VFE_LINE_RDI0=0,VFE_LINE_RDI1=1,VFE_LINE_RDI2=2,VFE_LINE_PIX=3};
enum {VFE_OUTPUT_OFF=0,VFE_OUTPUT_RESERVED=1,VFE_OUTPUT_ON=6};
enum vb2_buffer_state {VB2_BUF_STATE_DONE=0,VB2_BUF_STATE_ERROR=1};
struct vb2_buffer {uint64_t timestamp;struct camss_buffer *owner;};
struct camss_buffer {struct pipeline *core;EldenCameraBuffer view;uint32_t state;u32 addr[1];struct {struct vb2_buffer vb2_buf;uint32_t sequence;} vb;};
struct vfe_output {struct pipeline *core;u8 wm_idx[1];struct camss_buffer *buf[2],*pending[8];unsigned pending_head,pending_count;
    struct {int active_num;} gen2;unsigned state,sequence;bool frame_started;};
struct v4l2_pix_format_mplane {uint32_t height;struct {uint32_t bytesperline;} plane_fmt[1];};
struct camss_video {struct {struct {struct v4l2_pix_format_mplane pix_mp;} fmt;} active_fmt;struct pipeline *core;};
struct vfe_line {enum vfe_line_id id;struct pipeline *core;struct camss_video video_out;struct vfe_output output;struct {void *devnode;} subdev;};
struct vfe_device;
struct vfe_hw_ops {void (*frame_start)(struct vfe_device *,int);void (*vfe_wm_update)(struct vfe_device *,u8,u32,struct vfe_line *);void (*reg_update)(struct vfe_device *,enum vfe_line_id);};
struct vfe_resources {const struct vfe_hw_ops *hw_ops;};
struct vfe_device {struct camss *camss;u8 id;bool lite;enum vfe_line_id wm_output_map[4];struct vfe_line line[4];unsigned output_lock;struct vfe_resources *res;};
struct v4l2_event {uint32_t type;union {struct {uint32_t frame_sequence;} frame_sync;} u;};
struct pipeline {
    uint32_t magic,busy,cleanup,changed,armed,started,stopped,error;EldenCameraPipelineConfig config;EldenCameraPipelineIo io;EldenCameraPipelineStatus status;
    struct camss camss;struct camss_resources resources;struct csiphy_device phy;struct csiphy_device_regs phy_regs;
    struct csi2_config phy_lanes;struct csiphy_config phy_config;
    struct csid_device csid;struct csid_resources csid_res;struct csid_formats formats;struct csid_format_info format;
    struct vfe_device vfe;struct vfe_resources vfe_res;struct vfe_hw_ops vfe_ops;
    struct camss_buffer buffers[8];EldenCameraFrame completed[8];uint32_t completed_head,completed_count;
    uint64_t completed_sequence;uint32_t has_sequence;uint32_t saved_phy5,saved_phy6,saved_top_mask,saved_buf_mask,saved_rdi_mask[5],saved_cgc;
};
_Static_assert(sizeof(struct pipeline)<=sizeof(EldenCameraPipeline),"opaque pipeline size");
static struct pipeline *pipe_core(EldenCameraPipeline *p){return (struct pipeline *)(void *)p;}
static int pipe_failure(struct pipeline *p,int error){if(!p->error)p->error=(uint32_t)(error<0?error:-13);p->status.state=EldenPipeHeld;p->status.error=p->error;return (int)p->error;}
static int pipe_admit(struct pipeline *p,unsigned operation){int rc=p->io.admit(p->io.context,&p->config,operation);return rc<=0?rc:-13;}
static uint32_t pipe_read(struct pipeline *p,unsigned block,uint32_t offset){
    if(p->error)return 0;int rc=pipe_admit(p,p->cleanup?EldenPipeCleanup:EldenPipeCapture);uint32_t value=0;
    if(!rc)rc=p->io.read(p->io.context,block,offset,&value);if(rc)pipe_failure(p,rc);return value;
}
static void pipe_write(struct pipeline *p,unsigned block,uint32_t offset,uint32_t value){
    if(p->error)return;int rc=pipe_admit(p,p->cleanup?EldenPipeCleanup:EldenPipeConfigure);
    if(!rc){p->changed=1;rc=p->io.write(p->io.context,block,offset,value);}if(rc)pipe_failure(p,rc);
}
static int pipe_restore(struct pipeline *p,unsigned block,uint32_t reg,uint32_t value){
    int rc=pipe_admit(p,EldenPipeCleanup);uint32_t seen=0;
    if(!rc)rc=p->io.write(p->io.context,block,reg,value);if(!rc)rc=p->io.read(p->io.context,block,reg,&seen);
    return rc?rc:seen!=value?-71:0;
}
static void pipe_csiphy_write(struct csiphy_device *s,uint32_t reg,uint32_t v){pipe_write(s->camss->core,EldenCameraPhy,reg,v);}
static uint32_t pipe_csid_read(struct csid_device *s,uint32_t reg){return pipe_read(s->camss->core,EldenCameraCsid,reg);}
static void pipe_csid_write(struct csid_device *s,uint32_t reg,uint32_t v){pipe_write(s->camss->core,EldenCameraCsid,reg,v);}
static void pipe_vfe_write(struct vfe_device *s,uint32_t reg,uint32_t v){pipe_write(s->camss->core,EldenCameraVfe,reg,v);}
#define csid_is_lite(s) ((s)->lite)
#define vfe_is_lite(s) ((s)->lite)
#include "PhyRegisters.inc"
#include "PhyLaneTable.inc"
#include "CsidRegisters.inc"
#include "VfeRegisters.inc"
static void pipe_delay(struct pipeline *p,unsigned us){int rc=p->io.delay_us(p->io.context,us);if(rc)pipe_failure(p,rc);}
static u8 csiphy_settle_cnt_calc(s64 link,u32 rate){
    uint64_t ui=UINT64_C(1000000000000)/(uint64_t)link/2,period=UINT64_C(1000000000000)/rate;
    return (u8)((85000+6*ui)/period-6); // input validated before call, same original integer arithmetic
}
static const struct csid_format_info *csid_get_fmt_entry(const struct csid_format_info *formats,unsigned count,u32 code){for(unsigned i=0;i<count;i++)if(formats[i].code==code)return &formats[i];return NULL;}
static void complete(struct completion *c){c->done=1;}
static void reinit_completion(struct completion *c){c->done=0;}
static void camss_buf_done(struct camss *,int,int);static void camss_frame_start(struct camss *,int,int);
static unsigned long wait_for_completion_timeout(struct completion *,unsigned long);
#define msecs_to_jiffies(ms) ((unsigned long)(ms))
#define dev_err_ratelimited(...) pipe_failure(vfe->camss->core,-71)
#define spin_lock_irqsave(lock,flags) do{(void)(lock);(flags)=0;}while(0)
#define spin_unlock_irqrestore(lock,flags) do{(void)(lock);(void)(flags);}while(0)
static void camss_reg_update(struct camss *,int,int,bool);
static struct vfe_device *to_vfe(struct vfe_line *l){return &l->core->vfe;}
#define container_of(ptr,type,member) ((type *)((ptr)->core==NULL?NULL:&((ptr)->core->vfe.line[(ptr)->core->config.rdi])))
static struct camss_buffer *vfe_buf_get_pending(struct vfe_output *o){
    if(!o->pending_count)return NULL;struct camss_buffer *p=o->pending[o->pending_head];o->pending_head=(o->pending_head+1)%8;o->pending_count--;p->state=2;return p;
}
static void vfe_buf_add_pending(struct vfe_output *o,struct camss_buffer *b){if(o->pending_count==8){pipe_failure(o->core,-75);return;}o->pending[(o->pending_head+o->pending_count)%8]=b;o->pending_count++;b->state=1;}
static void v4l2_event_queue(void *pointer,const struct v4l2_event *e){
    struct pipeline *p=pointer;(void)e;if(!p->vfe.line[p->config.rdi].output.sequence)pipe_failure(p,-75);
}
static void vb2_buffer_done(void *pointer,unsigned state);
#define usleep_range(low,high) do{(void)(high);pipe_delay(csiphy->camss->core,(low));}while(0)
#define udelay(us) pipe_delay(csiphy->camss->core,(us))
#include "PhyMethods.inc"
#undef usleep_range
#undef udelay
#include "CsidMethods.inc"
#include "VfeMethods.inc"
#define ktime_get_ns() (vfe->camss->core->io.now_us(vfe->camss->core->io.context)*1000)
#include "VfeQueueMethods.inc"
#undef ktime_get_ns
static void camss_reg_update(struct camss *c,int id,int rdi,bool clear){if(id!=(int)c->core->config.decoder||rdi!=(int)c->core->config.rdi){pipe_failure(c->core,-116);return;}csid_subdev_reg_update(&c->core->csid,rdi,clear);}
static void camss_buf_done(struct camss *c,int id,int rdi){if(id!=(int)c->core->config.decoder||rdi!=(int)c->core->config.rdi){pipe_failure(c->core,-116);return;}vfe_buf_done(&c->core->vfe,rdi);}
static void camss_frame_start(struct camss *c,int id,int rdi){if(id!=(int)c->core->config.decoder||rdi!=(int)c->core->config.rdi){pipe_failure(c->core,-116);return;}vfe_frame_start(&c->core->vfe,rdi);}
static void vb2_buffer_done(void *pointer,unsigned error){
    struct vb2_buffer *vb=pointer;struct camss_buffer *b=vb?vb->owner:NULL;struct pipeline *p=b?b->core:NULL;
    if(!p||p->error)return;
    if(!b||b->state!=2||p->completed_count==8||!p->vfe.line[p->config.rdi].output.sequence||
       (p->has_sequence&&b->vb.sequence<=p->completed_sequence)){pipe_failure(p,-71);return;}
    int rc=pipe_admit(p,EldenPipeCapture);if(!rc)rc=p->io.buffer(p->io.context,&p->config,&b->view,1);if(rc){pipe_failure(p,rc);return;}
    uint64_t time=b->vb.vb2_buf.timestamp;if(!time||(p->status.last_ns&&time<p->status.last_ns)){pipe_failure(p,-110);return;}
    p->status.last_ns=time;p->completed_sequence=b->vb.sequence;p->has_sequence=1;
    p->completed[(p->completed_head+p->completed_count)%8]=(EldenCameraFrame){b->view,b->vb.sequence,time,p->status.frame_bytes,error};
    p->completed_count++;b->state=3;p->status.frames++;
}
static unsigned long wait_for_completion_timeout(struct completion *c,unsigned long timeout){
    struct pipeline *p=c->core;uint64_t start=p->io.now_us(p->io.context),last=start;
    for(unsigned i=0;i<50000;i++){
        csid_isr(0,&p->csid);if(p->error)return 0;if(c->done){p->status.reset_seen=1;return 1;}
        uint64_t now=p->io.now_us(p->io.context);if(now<last||now-start>=timeout*1000){pipe_failure(p,-110);return 0;}last=now;
        int rc=p->io.delay_us(p->io.context,10);if(rc){pipe_failure(p,rc);return 0;}
    }pipe_failure(p,-110);return 0;
}
static bool pipe_config_valid(const EldenCameraPipelineConfig *c){
    if(!c||!c->owner||!c->epoch||!c->route_epoch||c->sensor>1||c->decoder>4||c->rdi>(c->decoder<3?2u:3u)||
       c->timer_hz!=400000000)return false;
    const EldenCameraMode *m=&c->mode;
    return m->width==(c->sensor?3264u:4080u)&&m->height==(c->sensor?2448u:3072u)&&m->link_hz==(c->sensor?336000000u:672000000u)&&
       m->pixel_hz==(c->sensor?281600000u:558933333u)&&m->expected_sensor_id==(c->sensor?0x08a8:0x38ee)&&m->rotation==(c->sensor?270:90)&&
       m->master==c->sensor&&m->slave==(c->sensor?0x31:0x56)&&m->reset_gpio==(c->sensor?7:11)&&m->mclk==(c->sensor?4:3)&&
       m->lanes==4&&m->raw_bits==10&&m->bayer==(c->sensor?EldenCameraRggb:EldenCameraGrbg);
}
int elden_camera_pipeline_resources(const EldenCameraPipelineConfig *c,uint64_t pa[3],uint32_t bytes[3],uint32_t irq[3]){
    if(!pipe_config_valid(c)||!pa||!bytes||!irq)return -22;
    const uint64_t csid[]={0xad27000,0xad2a000,0xad2d000,0xad6d000,0xad72000},vfe[]={0xac86000,0xac96000,0xaca6000,0xad6e000,0xad73000};
    const uint32_t csidirq[]={633,635,463,637,408},vfeirq[]={465,468,489,638,409};
    pa[0]=c->sensor?0xadb1000:0xadaf000;pa[1]=csid[c->decoder];pa[2]=vfe[c->decoder];bytes[0]=0x2000;bytes[1]=c->decoder<3?0x2b00:0xa00;bytes[2]=c->decoder<3?0x10000:0x3000;
    irq[0]=c->sensor?154:480;irq[1]=csidirq[c->decoder];irq[2]=vfeirq[c->decoder];return 0;
}
static int pipe_enter(EldenCameraPipeline *storage){if(!storage||pipe_core(storage)->magic!=0x43414d34)return -22;uint32_t none=0;return __atomic_compare_exchange_n(&pipe_core(storage)->busy,&none,1u,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:-16;}
static int pipe_leave(struct pipeline *p,int rc){__atomic_store_n(&p->busy,0u,__ATOMIC_RELEASE);return rc;}
int elden_camera_pipeline_open(EldenCameraPipeline *storage,const EldenCameraPipelineConfig *cfg,const EldenCameraPipelineIo *io){
    if(!storage||pipe_core(storage)->magic||!pipe_config_valid(cfg)||!io||!io->admit||!io->read||!io->write||!io->now_us||!io->delay_us||!io->buffer||!io->drain)return -22;
    int rc=io->admit(io->context,cfg,EldenPipeAcquire);if(rc)return rc<0?rc:-13;
    pipe_zero(storage,sizeof(*storage));struct pipeline *p=pipe_core(storage);p->magic=0x43414d34;p->io=*io;p->config=*cfg;p->resources.version=CAMSS_8750;
    p->camss.core=p;p->camss.res=&p->resources;p->phy.camss=p->csid.camss=p->vfe.camss=&p->camss;
    p->phy_regs.offset=0x1000;p->phy_regs.common_status_offset=0x138;p->phy_regs.lane_regs=lane_regs_2_4_0;p->phy_regs.lane_array_size=(int)ARRAY_SIZE(lane_regs_2_4_0);
    p->phy.regs=&p->phy_regs;p->phy.timer_clk_rate=cfg->timer_hz;p->phy_lanes.lane_cfg.num_data=4;for(unsigned i=0;i<4;i++)p->phy_lanes.lane_cfg.data[i].pos=i;p->phy_config.csi2=&p->phy_lanes;
    p->csid.phy=(struct csid_phy_config){(u8)(cfg->sensor?4:3),4,0x3210,BIT(cfg->rdi)};p->csid.lite=cfg->decoder>=3;p->csid.reset_complete.core=p;
    p->csid.id=(u8)cfg->decoder;
    p->format=(struct csid_format_info){cfg->sensor?MEDIA_BUS_FMT_SRGGB10_1X10:MEDIA_BUS_FMT_SGRBG10_1X10,0x2b,2,10,1};p->formats.formats=&p->format;p->formats.nformats=1;p->csid_res.formats=&p->formats;p->csid.res=&p->csid_res;
    for(unsigned i=0;i<6;i++)p->csid.fmt[i].code=p->format.code;p->vfe.lite=p->csid.lite;p->vfe.id=(u8)cfg->decoder;
    p->vfe_ops=(struct vfe_hw_ops){vfe_frame_start,vfe_wm_update,vfe_reg_update};p->vfe_res.hw_ops=&p->vfe_ops;p->vfe.res=&p->vfe_res;
    for(unsigned i=0;i<4;i++){p->vfe.wm_output_map[i]=VFE_LINE_NONE;p->vfe.line[i].core=p;p->vfe.line[i].id=(enum vfe_line_id)i;p->vfe.line[i].output.core=p;p->vfe.line[i].video_out.core=p;p->vfe.line[i].subdev.devnode=p;}
    struct vfe_line *line=&p->vfe.line[cfg->rdi];p->status.stride=ALIGN(cfg->mode.width*10/8,16);p->status.frame_bytes=p->status.stride*cfg->mode.height;
    line->output.wm_idx[0]=(u8)cfg->rdi;
    line->video_out.active_fmt.fmt.pix_mp.height=cfg->mode.height;line->video_out.active_fmt.fmt.pix_mp.plane_fmt[0].bytesperline=p->status.stride;
    p->saved_phy5=pipe_read(p,EldenCameraPhy,0x1014);p->saved_phy6=pipe_read(p,EldenCameraPhy,0x1018);
    p->status.csid_version=pipe_read(p,EldenCameraCsid,0);p->status.vfe_version=pipe_read(p,EldenCameraVfe,0);
    struct csid_device *csid=&p->csid;struct vfe_device *vfe=&p->vfe;
    p->saved_top_mask=pipe_read(p,EldenCameraCsid,CSID_TOP_IRQ_MASK);p->saved_buf_mask=pipe_read(p,EldenCameraCsid,CSID_BUF_DONE_IRQ_MASK);
    for(unsigned i=0;i<(p->csid.lite?4u:5u);i++){p->saved_rdi_mask[i]=pipe_read(p,EldenCameraCsid,CSID_CSI2_RDIN_IRQ_MASK(i));
        if(pipe_read(p,EldenCameraCsid,CSID_RDI_CFG0(i))&RDI_CFG0_EN)return pipe_failure(p,-16);}
    /* Global CGC override touches the whole VFE bus. Check every exact
     * Gen4 write client, including non-RDI pixel/stats clients, before use. */
    for(unsigned i=0;i<(p->vfe.lite?6u:28u);i++)if(pipe_read(p,EldenCameraVfe,VFE_BUS_WM_CFG(i))&WM_CFG_EN)return pipe_failure(p,-16);
    p->saved_cgc=pipe_read(p,EldenCameraVfe,VFE_BUS_WM_CGC_OVERRIDE);
    if(pipe_read(p,EldenCameraCsid,CSID_TOP_IRQ_STATUS)||pipe_read(p,EldenCameraCsid,CSID_BUF_DONE_IRQ_STATUS))return pipe_failure(p,-16);
    for(unsigned i=0;i<(p->csid.lite?4u:5u);i++)if(pipe_read(p,EldenCameraCsid,CSID_CSI2_RDIN_IRQ_STATUS(i)))return pipe_failure(p,-16);
    if(p->error)return (int)p->error;if(p->saved_phy5||p->saved_phy6||!p->status.csid_version||p->status.csid_version==UINT32_MAX||!p->status.vfe_version||p->status.vfe_version==UINT32_MAX)return pipe_failure(p,-16);
    p->status.state=EldenPipeReady;return 0;
}
int elden_camera_pipeline_queue(EldenCameraPipeline *storage,const EldenCameraBuffer *view){
    if(!view||!view->id||!view->mapping)return -22;int rc=pipe_enter(storage);if(rc)return rc;struct pipeline *p=pipe_core(storage);
    if(p->error||p->stopped)return pipe_leave(p,-125);
    if(view->owner!=p->config.owner||view->epoch!=p->config.epoch||view->route_epoch!=p->config.route_epoch||!view->iova||(view->iova&255)||view->bytes<p->status.frame_bytes||view->bytes>UINT32_MAX||view->iova>UINT64_C(0x100000000)-view->bytes)return pipe_leave(p,-22);
    if((rc=pipe_admit(p,EldenPipeConfigure))||(rc=p->io.buffer(p->io.context,&p->config,view,0)))return pipe_leave(p,rc<0?rc:-13);
    struct camss_buffer *buffer=NULL;
    for(unsigned i=0;i<8;i++){
        struct camss_buffer *b=&p->buffers[i];if(b->view.id==view->id){if(b->state!=4)return pipe_leave(p,-16);buffer=b;}
        else if(b->view.id&&view->iova<b->view.iova+b->view.bytes&&b->view.iova<view->iova+view->bytes)return pipe_leave(p,-16);
    }
    if(!buffer)for(unsigned i=0;i<8;i++)if(!p->buffers[i].view.id){buffer=&p->buffers[i];break;}
    if(!buffer)return pipe_leave(p,-75);if(buffer->view.id&&(buffer->view.owner!=view->owner||buffer->view.epoch!=view->epoch||buffer->view.route_epoch!=view->route_epoch||buffer->view.iova!=view->iova||buffer->view.bytes!=view->bytes||buffer->view.mapping!=view->mapping))return pipe_leave(p,-116);
    buffer->core=p;buffer->vb.vb2_buf.owner=buffer;buffer->view=*view;buffer->addr[0]=(u32)view->iova;buffer->state=1;
    vfe_queue_buffer_v2(&p->vfe.line[p->config.rdi].video_out,buffer);
    for(unsigned i=0;i<2;i++)if(p->vfe.line[p->config.rdi].output.buf[i])p->vfe.line[p->config.rdi].output.buf[i]->state=2;
    return pipe_leave(p,p->error?(int)p->error:0);
}
int elden_camera_pipeline_start(EldenCameraPipeline *storage){
    int rc=pipe_enter(storage);if(rc)return rc;struct pipeline *p=pipe_core(storage);
    if(p->error||p->stopped||p->started)return pipe_leave(p,-125);struct vfe_line *line=&p->vfe.line[p->config.rdi];
    if(line->output.pending_count<2)return pipe_leave(p,-19);
    if((rc=pipe_admit(p,EldenPipeConfigure)))return pipe_leave(p,rc);
    csiphy_reset(&p->phy);if(p->error)return pipe_leave(p,(int)p->error);
    rc=csid_reset(&p->csid);if(rc||p->error)return pipe_leave(p,pipe_failure(p,rc?rc:(int)p->error));
    vfe_reg_update_clear(&p->vfe,(enum vfe_line_id)p->config.rdi);
    line->output.state=VFE_OUTPUT_ON;p->vfe.wm_output_map[p->config.rdi]=(enum vfe_line_id)p->config.rdi;p->armed=1;
    vfe_wm_start(&p->vfe,(u8)p->config.rdi,line);
    for(unsigned i=0;i<2;i++){line->output.buf[i]=vfe_buf_get_pending(&line->output);line->output.gen2.active_num++;vfe_wm_update(&p->vfe,(u8)p->config.rdi,line->output.buf[i]->addr[0],line);}
    csid_configure_stream(&p->csid,1);csiphy_lanes_enable(&p->phy,&p->phy_config,p->config.mode.link_hz,0xf);
    if(p->error)return pipe_leave(p,(int)p->error);p->started=1;p->status.started=1;p->status.state=EldenPipeStreaming;return pipe_leave(p,0);
}
int elden_camera_pipeline_service(EldenCameraPipeline *storage){int rc=pipe_enter(storage);if(rc)return rc;struct pipeline *p=pipe_core(storage);
    if(p->error||!p->started||p->stopped)return pipe_leave(p,-125);p->status.last_top_status=pipe_read(p,EldenCameraCsid,p->csid.lite?0x7c:0x84);p->status.last_buffer_status=pipe_read(p,EldenCameraCsid,p->csid.lite?0x8c:0xa4);
    uint32_t top=1u|BIT(p->csid.lite?13:3)|BIT((p->csid.lite?8:16)+p->config.rdi),buffers=BIT((p->csid.lite?1:16)+p->config.rdi);
    if((p->status.last_top_status&~top)||(p->status.last_buffer_status&~buffers))return pipe_leave(p,pipe_failure(p,-71));
    struct csid_device *csid=&p->csid;uint32_t rdi=pipe_read(p,EldenCameraCsid,CSID_CSI2_RDIN_IRQ_STATUS(p->config.rdi));
    if(rdi&~(INFO_CAMIF_SOF|INFO_RUP_DONE))return pipe_leave(p,pipe_failure(p,-71));
    csid_isr(0,&p->csid);return pipe_leave(p,p->error?(int)p->error:0);}
int elden_camera_pipeline_frame(EldenCameraPipeline *storage,EldenCameraFrame *out){if(!out)return -22;int rc=pipe_enter(storage);if(rc)return rc;struct pipeline *p=pipe_core(storage);
    if(p->error)return pipe_leave(p,(int)p->error);if(!p->completed_count)return pipe_leave(p,-11);
    if(p->stopped)return pipe_leave(p,-125);rc=pipe_admit(p,EldenPipeCapture);if(rc)return pipe_leave(p,rc);
    *out=p->completed[p->completed_head];p->completed_head=(p->completed_head+1)%8;p->completed_count--;
    for(unsigned i=0;i<8;i++)if(p->buffers[i].view.id==out->buffer.id)p->buffers[i].state=4;return pipe_leave(p,0);}
int elden_camera_pipeline_ready(EldenCameraPipeline *storage){int rc=pipe_enter(storage);if(rc)return rc;struct pipeline *p=pipe_core(storage);rc=p->started&&!p->error&&!p->stopped?pipe_admit(p,EldenPipeCapture):-19;return pipe_leave(p,rc);}
int elden_camera_pipeline_stop(EldenCameraPipeline *storage){int rc=pipe_enter(storage);if(rc)return rc;struct pipeline *p=pipe_core(storage);
    if(p->stopped)return pipe_leave(p,0);p->cleanup=1;rc=pipe_admit(p,EldenPipeCleanup);if(rc){p->cleanup=0;return pipe_leave(p,rc);}
    uint32_t prior=p->error;if(prior&&!p->changed){p->cleanup=0;return pipe_leave(p,(int)prior);}p->error=0;
    if(p->changed){csid_configure_stream(&p->csid,0);vfe_wm_stop(&p->vfe,(u8)p->config.rdi);csiphy_lanes_disable(&p->phy,&p->phy_config);}
    if(p->error)rc=(int)p->error;else {rc=p->io.drain(p->io.context,&p->config);if(rc>0)rc=-13;}
    if(!rc){
        struct csid_device *csid=&p->csid;struct vfe_device *vfe=&p->vfe;
        if(pipe_read(p,EldenCameraCsid,CSID_RDI_CTRL(p->config.rdi))||pipe_read(p,EldenCameraCsid,CSID_RDI_CFG0(p->config.rdi))&RDI_CFG0_EN||
           pipe_read(p,EldenCameraVfe,VFE_BUS_WM_CFG(RDI_WM(p->config.rdi)))&WM_CFG_EN||pipe_read(p,EldenCameraPhy,0x1014)||pipe_read(p,EldenCameraPhy,0x1018))rc=-16;
        if(!rc&&p->error)rc=(int)p->error;
        if(!rc)rc=pipe_restore(p,EldenCameraCsid,CSID_TOP_IRQ_MASK,p->saved_top_mask);
        if(!rc)rc=pipe_restore(p,EldenCameraCsid,CSID_BUF_DONE_IRQ_MASK,p->saved_buf_mask);
        for(unsigned i=0;!rc&&i<(p->csid.lite?4u:5u);i++)rc=pipe_restore(p,EldenCameraCsid,CSID_CSI2_RDIN_IRQ_MASK(i),p->saved_rdi_mask[i]);
        if(!rc)rc=pipe_restore(p,EldenCameraVfe,VFE_BUS_WM_CGC_OVERRIDE,p->saved_cgc);
        if(!rc)rc=pipe_admit(p,EldenPipeCleanup);
    }
    if(!rc){p->stopped=1;p->status.stopped=1;p->status.state=EldenPipeStopped;p->started=0;p->vfe.line[p->config.rdi].output.state=VFE_OUTPUT_OFF;p->vfe.line[p->config.rdi].output.gen2.active_num=0;}else pipe_failure(p,rc);
    if(!p->error)p->error=prior;p->status.error=p->error;p->cleanup=0;return pipe_leave(p,rc);
}
int elden_camera_pipeline_status(EldenCameraPipeline *storage,EldenCameraPipelineStatus *out){if(!out)return -22;int rc=pipe_enter(storage);if(rc)return rc;struct pipeline *p=pipe_core(storage);
    p->status.buffers=0;for(unsigned i=0;i<8;i++)if(p->buffers[i].view.id)p->status.buffers++;p->status.queued=p->vfe.line[p->config.rdi].output.pending_count;p->status.active=(uint32_t)p->vfe.line[p->config.rdi].output.gen2.active_num;p->status.completed=p->completed_count;
    p->status.sequence=p->vfe.line[p->config.rdi].output.sequence;*out=p->status;return pipe_leave(p,0);}
