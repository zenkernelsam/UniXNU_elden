/* SPDX-License-Identifier: GPL-2.0-only */
#include "Clock.h"
#include <stdbool.h>
#include <stddef.h>
#define BIT(n) (UINT32_C(1)<<(n))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define EBUSY 16
#define ETIMEDOUT 110
#define EINVAL 22
#define ENOENT 2
#define CMD_REG 0u
#define CMD_UPDATE BIT(0)
#define CMD_ROOT_EN BIT(1)
#define CMD_ROOT_OFF BIT(31)
#define CFG_REG 4u
#define CFG_SRC_DIV_SHIFT 0u
#define CFG_SRC_SEL_SHIFT 8u
#define CFG_SRC_SEL_MASK (7u<<8)
#define CFG_MODE_SHIFT 12u
#define CFG_MODE_MASK (3u<<12)
#define CFG_MODE_DUAL_EDGE (2u<<12)
#define CFG_HW_CLK_CTRL_MASK BIT(20)
#define RCG_CFG_OFFSET(r) ((r)->cmd_rcgr+4)
#define RCG_M_OFFSET(r) ((r)->cmd_rcgr+8)
#define RCG_N_OFFSET(r) ((r)->cmd_rcgr+12)
#define RCG_D_OFFSET(r) ((r)->cmd_rcgr+16)
#define CBCR_CLK_OFF BIT(31)
#define CBCR_NOC_FSM_STATUS (7u<<28)
#define FSM_STATUS_ON 2u
#define BRANCH_VOTED BIT(7)
#define BRANCH_HALT 0u
#define BRANCH_HALT_ENABLE 1u
#define BRANCH_HALT_DELAY 2u
#define BRANCH_HALT_SKIP 3u
#define FIELD_GET(mask,v) (((v)&(mask))>>28)
#define POLL_CFG_GDSCR BIT(5)
#define CFG_GDSCR_OFFSET 4u
#define GDSC_POWER_UP_COMPLETE BIT(16)
#define GDSC_POWER_DOWN_COMPLETE BIT(15)
#define PWR_ON_MASK BIT(31)
#define SW_COLLAPSE_MASK BIT(0)
#define GDSC_RETAIN_FF_ENABLE BIT(11)
#define ROOT_MASK (CFG_SRC_SEL_MASK|CFG_MODE_MASK|CFG_HW_CLK_CTRL_MASK|31u)
typedef uint32_t u32;typedef uint8_t u8;
struct root_spec {unsigned space;uint32_t reg,mnd_width,hid_width,safe_selector;};
struct branch_spec {unsigned space;uint32_t reg,halt;int root;uint32_t hwcg_reg,hwcg_bit;};
struct clock_spec {int branch;};
struct frequency_spec {unsigned root;uint32_t hz,selector,hid,m,n;};
struct domain_spec {uint32_t reg,wait_value;};
#include "ClockTopology.inc"
enum {ROOTS=ARRAY_SIZE(root_specs),BRANCHES=ARRAY_SIZE(branch_specs),PINS=8};
struct clocks;
struct regmap {struct clocks *core;unsigned space;};
struct clk_hw {void *object;unsigned kind,parents;const char *name;};
struct clk_regmap {struct regmap *regmap;struct clk_hw hw;uint32_t enable_reg,enable_mask,enable_is_inverted;};
struct parent_map {u8 src,cfg;};
struct freq_tbl {unsigned long freq;u8 src,pre_div;uint16_t m,n;};
struct clk_rcg2 {struct clk_regmap clkr;unsigned cmd_rcgr,mnd_width,hid_width,hw_clk_ctrl,safe_src_index;uint32_t parked_cfg;const struct parent_map *parent_map;};
struct clk_branch {struct clk_regmap clkr;uint32_t halt_reg,hwcg_reg,hwcg_bit,halt_check;};
struct gdsc {struct regmap *regmap;uint32_t flags,gdscr,gds_hw_ctrl,collapse_mask,collapse_ctrl;};
enum gdsc_status {GDSC_OFF,GDSC_ON};
struct root_state {struct clk_rcg2 rcg;struct parent_map parents[8];uint32_t original[5],applied[4],hz,changed,restoring;uint64_t references;};
struct branch_state {struct clk_branch branch;uint32_t original,changed;};
struct resource {uint8_t users,active,dep_attempted,root_ref;uint32_t hz,observed_hz;EldenCameraClockRequest request;};
struct domain_state {struct gdsc gdsc;uint32_t original,original_status,users,changed,active;};
struct msm_pingroup {unsigned ctl_reg,io_reg,oe_bit,out_bit;};
struct soc {const struct msm_pingroup *groups;};
struct gpio_chip {struct clocks *core;};
struct msm_pinctrl {struct soc *soc;struct gpio_chip chip;uint32_t lock;};
struct pin_state {uint32_t original_ctl,original_io,applied_ctl,applied_io,changed;};
struct clocks {
    uint32_t magic,busy,ready,stopped,cleanup,snapshot,controllers,pins_ready;int error,io_error;
    EldenCameraClockIo io;struct regmap maps[3];struct root_state roots[ROOTS];struct branch_state branches[BRANCHES];
    struct resource clocks[EldenCamClockCount];struct domain_state domains[4];
    struct msm_pingroup groups[119];struct soc soc;struct msm_pinctrl pins;struct pin_state pin_state[PINS];
};
_Static_assert(sizeof(struct clocks)<=sizeof(EldenCameraClocks),"camera clock storage");
static int normalize(int rc){return rc<=0?rc:-13;}
static int access(struct clocks *c){int rc=normalize(c->io.admit(c->io.context,c->io.owner,c->io.epoch,c->cleanup?EldenCamClockCleanup:EldenCamClockObserve));
    if(!rc&&c->controllers)rc=normalize(c->io.controllers(c->io.context,c->io.owner,c->io.epoch,EldenCamDependencyObserve));return rc;}
static int latch(struct clocks *c,int rc){rc=normalize(rc);if(rc&&!c->io_error)c->io_error=rc;return rc;}
static int rd(struct clocks *c,unsigned space,uint32_t reg,uint32_t *out){
    if(!out||space>2||reg&3)return -22;*out=0;int rc=c->io_error?c->io_error:access(c);
    if(!rc)rc=normalize(c->io.read(c->io.context,space,reg,out));return latch(c,rc);
}
static int wr(struct clocks *c,unsigned space,uint32_t reg,uint32_t value){
    int rc=c->io_error?c->io_error:access(c);if(!rc)rc=normalize(c->io.write(c->io.context,space,reg,value));return latch(c,rc);
}
static int regmap_read(struct regmap *m,unsigned reg,uint32_t *out){return rd(m->core,m->space,reg,out);}
static int regmap_write(struct regmap *m,unsigned reg,uint32_t value){return wr(m->core,m->space,reg,value);}
static int update(struct clocks *c,unsigned space,uint32_t reg,uint32_t mask,uint32_t value){uint32_t before=0,after=0;int rc=rd(c,space,reg,&before);
    if(!rc)rc=wr(c,space,reg,(before&~mask)|(value&mask));if(!rc)rc=rd(c,space,reg,&after);
    return rc?rc:(after&mask)!=(value&mask)?latch(c,-71):0;
}
static int regmap_update_bits(struct regmap *m,unsigned reg,uint32_t mask,uint32_t value){
    // CMD_UPDATE self-clears; verifying it as a persistent bit is incorrect.
    bool root_command=false;for(unsigned i=0;i<ROOTS;i++)root_command|=root_specs[i].space==m->space&&root_specs[i].reg==reg;
    if(mask==CMD_UPDATE&&root_command){uint32_t old=0;int rc=regmap_read(m,reg,&old);return rc?rc:regmap_write(m,reg,old|value);}
    return update(m->core,m->space,reg,mask,value);
}
static struct clk_rcg2 *to_clk_rcg2(struct clk_hw *hw){return hw->object;}
static struct clk_branch *to_clk_branch(struct clk_hw *hw){return hw->object;}
static struct clk_regmap *to_clk_regmap(struct clk_hw *hw){return hw->kind==1?&to_clk_rcg2(hw)->clkr:&to_clk_branch(hw)->clkr;}
static int clk_hw_get_num_parents(struct clk_hw *hw){return (int)hw->parents;}
static const char *clk_hw_get_name(const struct clk_hw *hw){return hw->name;}
#define clamp_t(type,v,lo,hi) ((type)((v)<(lo)?(lo):(v)>(hi)?(hi):(v)))
/* Upstream diagnostics keep their evaluated arguments; returned errors are
 * retained by the owning core. Logging is the original executor's task. */
static void clock_warning(int condition,const char *format,...){(void)condition;(void)format;}
static void clock_error(const char *format,...){(void)format;}
#define WARN(...) clock_warning(__VA_ARGS__)
#define pr_err(...) clock_error(__VA_ARGS__)
static void delay_core(struct clocks *c,unsigned us){if(c->io_error)return;int rc=access(c);if(!rc)rc=normalize(c->io.delay_us(c->io.context,us));(void)latch(c,rc);}
static struct msm_pinctrl *gpiochip_get_data(struct gpio_chip *chip){return &chip->core->pins;}
static uint32_t msm_readl_io(struct msm_pinctrl *p,const struct msm_pingroup *g){uint32_t value=0;(void)rd(p->chip.core,EldenCamTlmm,g->io_reg,&value);return value;}
static uint32_t msm_readl_ctl(struct msm_pinctrl *p,const struct msm_pingroup *g){uint32_t value=0;(void)rd(p->chip.core,EldenCamTlmm,g->ctl_reg,&value);return value;}
static void msm_writel_io(uint32_t v,struct msm_pinctrl *p,const struct msm_pingroup *g){(void)update(p->chip.core,EldenCamTlmm,g->io_reg,2u,v);}
static void msm_writel_ctl(uint32_t v,struct msm_pinctrl *p,const struct msm_pingroup *g){(void)update(p->chip.core,EldenCamTlmm,g->ctl_reg,BIT(9),v);}
#define raw_spin_lock_irqsave(lock,flags) do{(void)(lock);(flags)=0;}while(0)
#define raw_spin_unlock_irqrestore(lock,flags) do{(void)(lock);(void)(flags);}while(0)
static int qcom_find_src_index(struct clk_hw *,const struct parent_map *,u8);
#include "ClockOriginal.inc"
static struct clocks *core(EldenCameraClocks *c){return (struct clocks *)(void *)c;}
static int fault(struct clocks *c,int rc){c->ready=0;if(!c->error)c->error=rc<0?rc:-13;return rc<0?rc:-13;}
static int enter(EldenCameraClocks *s,int cleanup){if(!s||core(s)->magic!=0x434c4b34)return -22;struct clocks *c=core(s);uint32_t zero=0;
    if(!__atomic_compare_exchange_n(&c->busy,&zero,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return -16;
    c->cleanup=(uint32_t)cleanup;c->io_error=0;
    int rc=normalize(c->io.lock(c->io.context,c->io.owner,c->io.epoch,1));if(rc){__atomic_store_n(&c->busy,0u,__ATOMIC_RELEASE);return rc;}
    return 0;
}
static int leave(struct clocks *c,int rc){int unlock=normalize(c->io.lock(c->io.context,c->io.owner,c->io.epoch,0));
    if(!rc)rc=c->io_error;if(!rc)rc=unlock;if(unlock)fault(c,unlock);c->cleanup=0;__atomic_store_n(&c->busy,0u,__ATOMIC_RELEASE);return rc;
}
static int id(struct clocks *c,uint64_t owner,uint64_t epoch){return owner==c->io.owner&&epoch==c->io.epoch?0:-116;}
static int helper(struct clocks *c,int rc){return c->io_error?c->io_error:normalize(rc);}
static const struct frequency_spec *frequency(unsigned root,uint32_t hz){for(unsigned i=0;i<ARRAY_SIZE(frequencies);i++)if(frequencies[i].root==root&&frequencies[i].hz==hz)return &frequencies[i];return NULL;}
int elden_camera_clock_rate_supported(unsigned clock,uint32_t hz){if(clock>=EldenCamClockCount||!hz)return 0;
    int branch=clock_specs[clock].branch,root=branch<0?-1:branch_specs[branch].root;return root<0||frequency((unsigned)root,hz)!=NULL;
}
static uint32_t rate(uint32_t parent,const EldenCameraClockRequest *r){uint64_t v=parent;if(r->hid)v=v*2/(r->hid+1);if(r->n)v=v*r->m/r->n;return v>UINT32_MAX?0:(uint32_t)v;}
static int dependency(struct clocks *c,struct resource *r,int action){uint32_t actual=0;int rc=normalize(c->io.dependency(c->io.context,c->io.owner,c->io.epoch,&r->request,action,&actual));
    if(!rc&&action!=EldenCamDependencyRelease){uint32_t observed=r->request.root==UINT32_MAX?actual:rate(actual,&r->request);
        uint32_t delta=observed>r->hz?observed-r->hz:r->hz-observed;
        // Exact upstream integer F table rounds some fractional divisors by
        // one Hertz. Publish actual decoded rate; never accept missing/loose
        // PLL rates or change the nominal native CCI/MCLK expectations.
        if(!observed||delta>1)rc=-34;else r->observed_hz=observed;}return rc;
}
static int snapshot_root(struct clocks *c,unsigned index,uint32_t values[5]){struct root_state *r=&c->roots[index];for(unsigned i=0;i<5;i++){
    int rc=rd(c,root_specs[index].space,r->rcg.cmd_rcgr+4*i,&values[i]);if(rc)return rc;}
    return values[0]&0xf1?-16:0;
}
static int root_live(struct clocks *c,unsigned index){struct root_state *r=&c->roots[index];uint32_t v[5];int rc=snapshot_root(c,index,v);if(rc)return rc;
    if(r->references){if(v[1]!=r->applied[0])return -116;
        uint32_t mask=r->rcg.mnd_width?BIT(r->rcg.mnd_width)-1:0;
        if(v[1]&CFG_MODE_MASK)for(unsigned i=0;i<3;i++)if((v[i+2]&mask)!=(r->applied[i+1]&mask))return -116;
    }return 0;
}
static int observe(struct clocks *c,unsigned clock){struct resource *r=&c->clocks[clock];if(!r->users||!r->active)return -19;
    int rc=dependency(c,r,EldenCamDependencyObserve);if(rc)return rc;int branch=clock_specs[clock].branch;
    if(branch>=0){uint32_t v=0;rc=rd(c,branch_specs[branch].space,branch_specs[branch].reg,&v);if(rc)return rc;
        if(!(v&1))return -116;int on=clk_branch2_check_halt(&c->branches[branch].branch,true);if(c->io_error)return c->io_error;if(!on)return -116;
        int root=branch_specs[branch].root;if(root>=0){uint32_t cmd=0;rc=rd(c,root_specs[root].space,root_specs[root].reg,&cmd);
            if(rc)return rc;if(cmd&CMD_ROOT_OFF)return -116;return root_live(c,(unsigned)root);}
    }return 0;
}
static int unused_root(struct clocks *c,unsigned root){
    // A gate's enable bit is not a real consumer/DMA proof. The original
    // provider must prove all sibling users physically idle and hold the root
    // lease; shared RCG force-enable permits retained, inactive boot gates.
    return normalize(c->io.root_unused(c->io.context,c->io.owner,c->io.epoch,root));
}
static int root_acquire(struct clocks *c,unsigned index,unsigned clock,const struct frequency_spec *f){struct root_state *r=&c->roots[index];int rc=root_live(c,index);if(rc)return rc;
    if(r->references){if(r->hz!=f->hz)return -16;r->references|=UINT64_C(1)<<clock;return 0;}
    uint32_t current[5];rc=snapshot_root(c,index,current);if(rc)return rc;
    if((current[1]&ROOT_MASK)!=(r->original[1]&ROOT_MASK))return -116;
    struct freq_tbl entry={f->hz,(uint8_t)f->selector,(uint8_t)f->hid,(uint16_t)f->m,(uint16_t)f->n};
    uint32_t cfg=current[1];rc=helper(c,__clk_rcg2_configure_parent(&r->rcg,entry.src,&cfg));if(rc)return rc;
    // Refuse changing a root while any foreign branch is live. Matching
    // original boot configuration can be borrowed without reprogramming it.
    uint32_t desired=(cfg&~(31u|CFG_MODE_MASK|CFG_HW_CLK_CTRL_MASK))|f->hid|(f->n&&f->m!=f->n?CFG_MODE_DUAL_EDGE:0);
    bool changing=(current[1]&ROOT_MASK)!=(desired&ROOT_MASK);
    uint32_t mask=r->rcg.mnd_width?BIT(r->rcg.mnd_width)-1:0;
    uint32_t m=current[2],n=current[3],d=current[4];
    if(f->n){m=(m&~mask)|(f->m&mask);n=(n&~mask)|(~(f->n-f->m)&mask);d=(d&~mask)|(~clamp_t(u32,f->n,f->m,2*(f->n-f->m))&mask);
        changing|=(current[2]&mask)!=(m&mask)||(current[3]&mask)!=(n&mask)||(current[4]&mask)!=(d&mask);}
    if(changing){rc=unused_root(c,index);if(rc)return rc;}
    r->references|=UINT64_C(1)<<clock;r->hz=f->hz;
    r->applied[0]=desired;r->applied[1]=m;r->applied[2]=n;r->applied[3]=d;
    if(changing){r->changed=1;r->rcg.parked_cfg=current[1];
        rc=helper(c,__clk_rcg2_configure(&r->rcg,&entry,&r->rcg.parked_cfg));if(!rc)rc=helper(c,clk_rcg2_shared_enable(&r->rcg.clkr.hw));if(rc)return rc;
    }
    uint32_t value[5];rc=snapshot_root(c,index,value);if(rc)return rc;
    if((value[1]&ROOT_MASK)!=(desired&ROOT_MASK))return -71;
    if(f->n)for(unsigned i=0;i<3;i++)if((value[i+2]&mask)!=(r->applied[i+1]&mask))return -71;
    return 0;
}
static int root_restore(struct clocks *c,unsigned root){struct root_state *r=&c->roots[root];if(r->references)return -16;if(!r->changed)return 0;
    int rc=unused_root(c,root);if(rc)return rc;uint32_t v[5];rc=snapshot_root(c,root,v);if(rc)return rc;
    uint32_t allowed[]={r->applied[0]&ROOT_MASK,r->original[1]&ROOT_MASK,r->rcg.safe_src_index<<8};
    if((v[1]&ROOT_MASK)!=allowed[0]&&(v[1]&ROOT_MASK)!=allowed[1]&&(v[1]&ROOT_MASK)!=allowed[2])return -116;
    if((v[1]&~ROOT_MASK)!=(r->original[1]&~ROOT_MASK)&&v[1]!=(r->rcg.safe_src_index<<8))return -116;
    uint32_t mask=r->rcg.mnd_width?BIT(r->rcg.mnd_width)-1:0;
    for(unsigned i=0;i<3;i++)if((v[i+2]&mask)!=(r->applied[i+1]&mask)&&(v[i+2]&mask)!=(r->original[i+2]&mask))return -116;
    // Actual original shared-disable parks safely; the parent provider stays
    // held while the prior snapshot is then restored through real updates.
    r->restoring=1;clk_rcg2_shared_disable(&r->rcg.clkr.hw);rc=helper(c,0);if(rc)return rc;
    rc=helper(c,clk_rcg2_set_force_enable(&r->rcg.clkr.hw));if(rc)return rc;
    for(unsigned i=0;i<3&&!rc;i++)if(r->rcg.mnd_width)rc=update(c,root_specs[root].space,r->rcg.cmd_rcgr+8+4*i,BIT(r->rcg.mnd_width)-1,r->original[2+i]);
    // Original shared-disable writes the entire safe CFG. Restore the entire
    // owned original snapshot while force-enabled, including untouched bits;
    // restoring only rate bits would silently discard its other fields.
    if(!rc)rc=regmap_write(&c->maps[root_specs[root].space],r->rcg.cmd_rcgr+4,r->original[1]);
    if(!rc)rc=helper(c,update_config(&r->rcg));if(!rc)rc=helper(c,clk_rcg2_clear_force_enable(&r->rcg.clkr.hw));
    if(!rc)rc=update(c,root_specs[root].space,r->rcg.cmd_rcgr,CMD_ROOT_EN,r->original[0]);
    if(rc)return rc;rc=snapshot_root(c,root,v);if(rc)return rc;
    if(v[1]!=r->original[1])return -71;
    for(unsigned i=0;i<3;i++)if((v[i+2]&mask)!=(r->original[i+2]&mask))return -71;
    r->changed=0;r->restoring=0;r->hz=0;return 0;
}
int elden_camera_clocks_open(EldenCameraClocks *s,const EldenCameraClockIo *io){
    if(!s||core(s)->magic||!io||!io->owner||!io->epoch||!io->admit||!io->lock||!io->read||!io->write||!io->delay_us||!io->now_us||
       !io->controllers||!io->dependency||!io->consumer_idle||!io->sensor_idle||!io->root_unused||!io->domain_unused||!io->pins_unused)return -22;
    int rc=normalize(io->admit(io->context,io->owner,io->epoch,EldenCamClockAcquire));if(rc)return rc;
    uint8_t *b=(uint8_t *)s;for(unsigned i=0;i<sizeof(*s);i++)b[i]=0;struct clocks *c=core(s);c->magic=0x434c4b34;c->io=*io;
    rc=enter(s,0);if(rc)return rc;c->controllers=1;rc=normalize(io->controllers(io->context,io->owner,io->epoch,EldenCamDependencyAcquire));
    if(rc)return leave(c,fault(c,rc));for(unsigned i=0;i<3;i++)c->maps[i]=(struct regmap){c,i};
    for(unsigned i=0;i<ROOTS;i++){struct root_state *r=&c->roots[i];const struct root_spec *p=&root_specs[i];
        r->rcg=(struct clk_rcg2){.clkr={.regmap=&c->maps[p->space],.hw={&r->rcg,1,0,"EldenCameraRCG"}},
            .cmd_rcgr=p->reg,.mnd_width=p->mnd_width,.hid_width=p->hid_width,.safe_src_index=p->safe_selector,.parent_map=r->parents};
        for(unsigned f=0;f<ARRAY_SIZE(frequencies);f++)if(frequencies[f].root==i){unsigned sel=frequencies[f].selector;bool found=false;
            for(unsigned j=0;j<r->rcg.clkr.hw.parents;j++)found|=r->parents[j].src==sel;
            if(!found)r->parents[r->rcg.clkr.hw.parents++]=(struct parent_map){(u8)sel,(u8)sel};}
        rc=snapshot_root(c,i,r->original);if(rc)return leave(c,fault(c,rc));r->rcg.parked_cfg=r->original[1];
    }
    for(unsigned i=0;i<BRANCHES;i++){struct branch_state *r=&c->branches[i];const struct branch_spec *p=&branch_specs[i];
        r->branch=(struct clk_branch){.clkr={.regmap=&c->maps[p->space],.hw={&r->branch,2,0,"EldenCameraBranch"},.enable_reg=p->reg,.enable_mask=1},
            .halt_reg=p->halt,.hwcg_reg=p->hwcg_reg,.hwcg_bit=p->hwcg_bit,.halt_check=BRANCH_HALT};
        rc=rd(c,p->space,p->reg,&r->original);if(rc)return leave(c,fault(c,rc));
    }
    for(unsigned i=0;i<4;i++){c->domains[i].gdsc=(struct gdsc){&c->maps[0],POLL_CFG_GDSCR,domain_specs[i].reg,0,0,0};
        rc=rd(c,0,domain_specs[i].reg,&c->domains[i].original);
        if(!rc)rc=rd(c,0,domain_specs[i].reg+4,&c->domains[i].original_status);if(rc)return leave(c,fault(c,rc));
        unsigned states=c->domains[i].original_status&((1u<<16)|(1u<<15));
        bool on=states==(1u<<16),off=states==(1u<<15);uint32_t ctl=c->domains[i].original;
        if((!on&&!off)||(ctl&6u)||(on&&(ctl&1u))||(off&&!(ctl&1u)))return leave(c,fault(c,-116));}
    c->soc.groups=c->groups;c->pins=(struct msm_pinctrl){&c->soc,{c},0};
    for(unsigned i=0;i<ARRAY_SIZE(c->groups);i++)c->groups[i]=(struct msm_pingroup){i*0x1000,i*0x1000+4,9,1};
    c->snapshot=1;c->ready=1;return leave(c,0);
}
int elden_camera_clock_resource(EldenCameraClocks *s,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned clock,uint32_t hz,int on){
    if(sensor>1||clock>=EldenCamClockCount||(on!=0&&on!=1)||!hz)return -22;int rc=enter(s,!on);if(rc)return rc;struct clocks *c=core(s);
    if(id(c,owner,epoch))return leave(c,-116);if(c->stopped||!c->snapshot)return leave(c,-125);if(on&&!c->ready)return leave(c,c->error?c->error:-125);
    struct resource *r=&c->clocks[clock];uint8_t user=(uint8_t)BIT(sensor);int branch=clock_specs[clock].branch;int root=branch<0?-1:branch_specs[branch].root;
    if(!on&&!(r->users&user))return leave(c,0); // Refused unsupported requests never acquired a reference.
    const struct frequency_spec *f=root<0?NULL:frequency((unsigned)root,hz);if(root>=0&&!f)return leave(c,-34);
    if(r->users&&r->hz!=hz)return leave(c,-16);
    if(on){
        if(r->users){rc=observe(c,clock);
            if(rc)return leave(c,fault(c,rc));if(!(r->users&user)){r->users|=user;r->active|=user;}return leave(c,0);}
        if(root>=0&&c->roots[root].references&&c->roots[root].hz!=hz)return leave(c,-16);
        r->users=user;r->hz=hz;r->request=(EldenCameraClockRequest){clock,root<0?UINT32_MAX:(uint32_t)root,f?f->selector:UINT32_MAX,hz,
            f?f->m:0,f?f->n:0,f?f->hid:0,root<0?0:c->roots[root].original[1],root<0?0:c->roots[root].original[2],
            root<0?0:c->roots[root].original[3],root<0?0:c->roots[root].original[4]};
        r->dep_attempted=1;rc=dependency(c,r,EldenCamDependencyAcquire);if(rc)return leave(c,fault(c,rc));
        if(root>=0){r->root_ref=1;rc=root_acquire(c,(unsigned)root,clock,f);if(rc)return leave(c,fault(c,rc));}
        if(branch>=0){struct branch_state *br=&c->branches[branch];uint32_t actual=0;rc=rd(c,branch_specs[branch].space,branch_specs[branch].reg,&actual);
            if(rc)return leave(c,fault(c,rc));if((actual&1)!=(br->original&1))return leave(c,fault(c,-116));
            if(!(actual&1)){br->changed=1;rc=helper(c,clk_branch2_enable(&br->branch.clkr.hw));}
            if(!rc)rc=rd(c,branch_specs[branch].space,branch_specs[branch].reg,&actual);
            if(!rc&&!(actual&1))rc=-71;if(!rc&&root>=0)rc=root_live(c,(unsigned)root);
            if(rc)return leave(c,fault(c,rc));}
        r->active=user;return leave(c,0);
    }
    if(!(r->users&user))return leave(c,0);
    rc=normalize(c->io.consumer_idle(c->io.context,owner,epoch,sensor,clock));if(rc)return leave(c,rc);
    if(r->users&(uint8_t)~user){r->users&=(uint8_t)~user;r->active&=(uint8_t)~user;return leave(c,0);}
    r->active=0;
    if(branch>=0){struct branch_state *br=&c->branches[branch];if(br->changed){uint32_t value=0;rc=rd(c,branch_specs[branch].space,branch_specs[branch].reg,&value);
        if(!rc&&value&1){clk_branch2_disable(&br->branch.clkr.hw);rc=helper(c,0);}if(!rc)rc=rd(c,branch_specs[branch].space,branch_specs[branch].reg,&value);
        if(!rc&&(value&1)!=(br->original&1))rc=-71;
        // Original void disable cannot return branch_wait's timeout. Require
        // actual halt status once more; enable-bit0 alone is not stopped.
        if(!rc&&!clk_branch2_check_halt(&br->branch,false))rc=-71;if(c->io_error)rc=c->io_error;
        if(rc)return leave(c,fault(c,rc));br->changed=0;}}
    if(root>=0&&r->root_ref){struct root_state *rr=&c->roots[root];rr->references&=~(UINT64_C(1)<<clock);
        if(!rr->references){rc=root_restore(c,(unsigned)root);if(rc)return leave(c,fault(c,rc));}r->root_ref=0;}
    if(r->dep_attempted){rc=dependency(c,r,EldenCamDependencyRelease);if(rc)return leave(c,fault(c,rc));r->dep_attempted=0;}
    r->users=0;r->hz=0;return leave(c,0);
}
int elden_camera_clock_observe(EldenCameraClocks *s,uint64_t owner,uint64_t epoch,unsigned clock,uint32_t *hz){
    if(clock>=EldenCamClockCount||!hz)return -22;*hz=0;int rc=enter(s,0);if(rc)return rc;struct clocks *c=core(s);
    if(id(c,owner,epoch)||!c->ready||c->stopped)return leave(c,-125);rc=observe(c,clock);if(!rc)*hz=c->clocks[clock].observed_hz;return leave(c,rc?fault(c,rc):0);
}
int elden_camera_clock_observe_cleanup(EldenCameraClocks *s,uint64_t owner,uint64_t epoch,unsigned clock,uint32_t *hz){
    if(clock>=EldenCamClockCount||!hz)return -22;*hz=0;int rc=enter(s,1);if(rc)return rc;struct clocks *c=core(s);
    if(id(c,owner,epoch)||!c->snapshot||c->stopped)return leave(c,-125);
    rc=observe(c,clock);if(!rc)*hz=c->clocks[clock].observed_hz;return leave(c,rc);
}
static int domain_wait(struct clocks *c,struct gdsc *g,int on){for(unsigned i=0;i<2001;i++){int rc=gdsc_check_status(g,on?GDSC_ON:GDSC_OFF);if(c->io_error)rc=c->io_error;
    if(rc<0)return rc;if(rc==1)return 0;if(i<2000)delay_core(c,1);}return -110;}
int elden_camera_domain_resource(EldenCameraClocks *s,uint64_t owner,uint64_t epoch,unsigned sensor,unsigned domain,int on){
    if(sensor>1||domain>3||(on!=0&&on!=1))return -22;int rc=enter(s,!on);if(rc)return rc;struct clocks *c=core(s);
    if(id(c,owner,epoch))return leave(c,-116);if(c->stopped||!c->snapshot||(on&&!c->ready))return leave(c,-125);
    struct domain_state *d=&c->domains[domain];uint32_t user=BIT(sensor),control=0;
    if(on){if(domain&&!c->domains[0].active)return leave(c,-19);
        if(d->users){rc=domain_wait(c,&d->gdsc,1);if(rc)return leave(c,fault(c,rc));d->users|=user;d->active|=user;return leave(c,0);}
        rc=normalize(c->io.domain_unused(c->io.context,owner,epoch,domain));if(rc)return leave(c,rc);
        d->users=user;rc=rd(c,0,d->gdsc.gdscr,&control);if(rc)return leave(c,fault(c,rc));
        if((control&0x00fff807u)!=(d->original&0x00fff807u))return leave(c,fault(c,-116));
        int observed=gdsc_check_status(&d->gdsc,GDSC_ON);if(c->io_error)return leave(c,fault(c,c->io_error));if(observed<0)return leave(c,fault(c,observed));
        if(observed&&(control&7u))return leave(c,fault(c,-116)); // transition/HW override is not settled ownership
        if(!observed){d->changed=1;rc=update(c,0,d->gdsc.gdscr,0x00fff006,domain_specs[domain].wait_value);
            if(!rc)rc=helper(c,gdsc_update_collapse_bit(&d->gdsc,false));if(!rc)rc=domain_wait(c,&d->gdsc,1);
            if(!rc){delay_core(c,1);gdsc_retain_ff_on(&d->gdsc);rc=helper(c,0);}if(rc)return leave(c,fault(c,rc));}
        d->active=user;return leave(c,0);
    }
    if(!(d->users&user))return leave(c,0);rc=normalize(c->io.sensor_idle(c->io.context,owner,epoch,sensor));if(rc)return leave(c,rc);
    if(!(d->users&~user))for(unsigned i=0;i<EldenCamClockCount;i++)if(c->clocks[i].users||c->clocks[i].dep_attempted)return leave(c,-16);
    if(!domain&&!(d->users&~user))for(unsigned i=1;i<4;i++)if(c->domains[i].users||c->domains[i].changed)return leave(c,-16);
    if(!(d->users&~user)){rc=normalize(c->io.domain_unused(c->io.context,owner,epoch,domain));if(rc)return leave(c,rc);}
    if(d->users&~user){d->users&=~user;d->active&=~user;return leave(c,0);}d->active=0;
    if(d->changed){uint32_t current=0;rc=rd(c,0,d->gdsc.gdscr,&current);if(rc)return leave(c,fault(c,rc));
        uint32_t applied=(d->original&~0x00fff807u)|domain_specs[domain].wait_value|BIT(11);
        uint32_t mask=0x00fff807u;
        uint32_t prepared=(d->original&~0x00fff006u)|domain_specs[domain].wait_value;
        if((current&mask)!=(applied&mask)&&(current&mask)!=(d->original&mask)&&
           (current&mask)!=(prepared&mask)&&(current&mask)!=((prepared&~1u)&mask)&&
           (current&mask)!=((applied|1u)&mask))return leave(c,fault(c,-116));
        rc=helper(c,gdsc_update_collapse_bit(&d->gdsc,true));if(!rc)rc=domain_wait(c,&d->gdsc,0);
        if(!rc)rc=update(c,0,d->gdsc.gdscr,0x00fff807,d->original);if(rc)return leave(c,fault(c,rc));d->changed=0;}
    d->users=0;return leave(c,0);
}
static const unsigned pin_numbers[PINS]={7,11,92,93,113,114,117,118};
int elden_camera_clock_register_allowed(unsigned space,uint32_t reg,int writing){
    if((reg&3)||(writing!=0&&writing!=1))return 0;
    if(space==EldenCamTlmm){for(unsigned i=0;i<PINS;i++)if(reg/0x1000==pin_numbers[i])return reg%0x1000==0||reg%0x1000==4||(!writing&&reg%0x1000==8);return 0;}
    if(space>1)return 0;
    for(unsigned i=0;i<ROOTS;i++)if(root_specs[i].space==space&&reg>=root_specs[i].reg&&reg<=root_specs[i].reg+16)return 1;
    for(unsigned i=0;i<BRANCHES;i++)if(branch_specs[i].space==space&&(reg==branch_specs[i].reg||(!writing&&reg==branch_specs[i].halt)))return 1;
    if(!space)for(unsigned i=0;i<4;i++)if(reg==domain_specs[i].reg||(!writing&&reg==domain_specs[i].reg+4))return 1;
    return 0;
}
static unsigned pin_mask(uint32_t original){return 3u|(15u<<2)|(7u<<6)|BIT(9)|((original&BIT(11))?BIT(12):0);}
int elden_camera_pins_prepare(EldenCameraClocks *s,uint64_t owner,uint64_t epoch){int rc=enter(s,0);if(rc)return rc;struct clocks *c=core(s);
    if(id(c,owner,epoch)||!c->ready||!c->snapshot||c->pins_ready)return leave(c,-125);
    for(unsigned sensor=0;sensor<2;sensor++){rc=normalize(c->io.pins_unused(c->io.context,owner,epoch,sensor));if(rc)return leave(c,rc);}
    // Observe all dedicated pins before first mutation, including no GPIO IRQ.
    for(unsigned i=0;i<PINS;i++){struct pin_state *p=&c->pin_state[i];unsigned off=pin_numbers[i]*0x1000;uint32_t irq=0;
        rc=rd(c,2,off,&p->original_ctl);if(!rc)rc=rd(c,2,off+4,&p->original_io);if(!rc)rc=rd(c,2,off+8,&irq);if(rc||irq&1)return leave(c,fault(c,rc?rc:-16));
        p->applied_ctl=p->original_ctl;p->applied_io=p->original_io;}
    for(unsigned i=0;i<PINS;i++){struct pin_state *p=&c->pin_state[i];unsigned pin=pin_numbers[i],off=pin*0x1000;
        unsigned value=i<2?BIT(9):i<4?4u:7u;if(p->original_ctl&BIT(11))value|=BIT(12);
        p->applied_ctl=(p->original_ctl&~pin_mask(p->original_ctl))|value;
        p->changed=1;if(i<2){p->applied_io&=~2u;msm_gpio_direction_output(&c->pins.chip,pin,0);rc=helper(c,0);if(rc)return leave(c,fault(c,rc));}
        rc=update(c,2,off,pin_mask(p->original_ctl),value);if(rc)return leave(c,fault(c,rc));
    }c->pins_ready=1;return leave(c,0);
}
int elden_camera_sensor_reset(EldenCameraClocks *s,uint64_t owner,uint64_t epoch,unsigned sensor,int asserted){
    if(sensor>1||(asserted!=0&&asserted!=1))return -22;int rc=enter(s,asserted);if(rc)return rc;struct clocks *c=core(s);
    if(id(c,owner,epoch)||!c->pins_ready||c->stopped||(!asserted&&!c->ready))return leave(c,-125);
    unsigned i=sensor?0:1,pin=pin_numbers[i];struct pin_state *p=&c->pin_state[i];uint32_t value=0;
    rc=rd(c,2,pin*0x1000,&value);if(!rc&&(value&pin_mask(p->original_ctl))!=(p->applied_ctl&pin_mask(p->original_ctl)))rc=-116;
    if(!rc){p->applied_io=(p->applied_io&~2u)|(!asserted?2u:0u);msm_gpio_set(&c->pins.chip,pin,!asserted);rc=helper(c,0);}
    return leave(c,rc?fault(c,rc):0);
}
int elden_camera_clocks_status(EldenCameraClocks *s,EldenCameraClockStatus *out){if(!out)return -22;int rc=enter(s,1);if(rc)return rc;struct clocks *c=core(s);EldenCameraClockStatus status={0};
    status.ready=c->ready;status.stopped=c->stopped;status.controller_attempted=c->controllers;status.pins_ready=c->pins_ready;status.error=c->error;
    for(unsigned i=0;i<EldenCamClockCount;i++){status.clock_users[i]=c->clocks[i].users;status.active_users[i]=c->clocks[i].active;}
    for(unsigned i=0;i<ROOTS;i++)if(c->roots[i].changed)status.changed_roots|=UINT64_C(1)<<i;
    for(unsigned i=0;i<BRANCHES;i++)if(c->branches[i].changed)status.changed_branches|=UINT64_C(1)<<i;
    for(unsigned i=0;i<4;i++){status.domain_users[i]=c->domains[i].users;if(c->domains[i].changed)status.changed_domains|=BIT(i);}
    for(unsigned i=0;i<PINS;i++)if(c->pin_state[i].changed)status.changed_pins|=BIT(i);*out=status;return leave(c,0);
}
int elden_camera_clocks_stop(EldenCameraClocks *s){int rc=enter(s,1);if(rc)return rc;struct clocks *c=core(s);if(c->stopped)return leave(c,0);
    for(unsigned i=0;i<EldenCamClockCount;i++)if(c->clocks[i].users||c->clocks[i].dep_attempted)return leave(c,-16);
    for(unsigned i=0;i<4;i++)if(c->domains[i].users||c->domains[i].changed)return leave(c,-16);
    for(unsigned i=0;i<ROOTS;i++)if(c->roots[i].references||c->roots[i].changed)return leave(c,-16);
    for(unsigned sensor=0;sensor<2;sensor++){rc=normalize(c->io.pins_unused(c->io.context,c->io.owner,c->io.epoch,sensor));if(rc)return leave(c,rc);}
    for(unsigned i=0;i<PINS;i++){struct pin_state *p=&c->pin_state[i];if(!p->changed)continue;unsigned reg=pin_numbers[i]*0x1000;uint32_t v=0;
        rc=rd(c,2,reg,&v);if(rc)return leave(c,fault(c,rc));unsigned mask=pin_mask(p->original_ctl);
        bool known=(v&mask)==(p->applied_ctl&mask)||(v&mask)==(p->original_ctl&mask);
        if(i<2)known|=(v&mask)==((p->original_ctl|BIT(9))&mask);
        if(i<2&&!(p->original_ctl&BIT(9)))known|=(v&mask)==((p->applied_ctl&~BIT(9))&mask);
        if(!known)return leave(c,fault(c,-116));
        if(i<2){uint32_t io=0;rc=rd(c,2,reg+4,&io);if(rc)return leave(c,fault(c,rc));
            if((io&2)!=(p->applied_io&2)&&(io&2)!=(p->original_io&2))return leave(c,fault(c,-116));
            // Disable output first when returning an originally input pin;
            // changing its prior output latch while GPIO still drives could
            // otherwise deassert reset for a moment during final retirement.
            if(!(p->original_ctl&BIT(9))){rc=update(c,2,reg,BIT(9),0);if(rc)return leave(c,fault(c,rc));}
            rc=update(c,2,reg+4,2,p->original_io);if(rc)return leave(c,fault(c,rc));}
        rc=update(c,2,reg,mask,p->original_ctl);if(rc)return leave(c,fault(c,rc));p->changed=0;
    }
    if(c->controllers){rc=normalize(c->io.controllers(c->io.context,c->io.owner,c->io.epoch,EldenCamDependencyRelease));if(rc)return leave(c,fault(c,rc));c->controllers=0;}
    c->ready=0;c->stopped=1;return leave(c,0);
}
