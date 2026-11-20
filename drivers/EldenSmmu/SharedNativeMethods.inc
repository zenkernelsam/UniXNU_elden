/* SPDX-License-Identifier: BSD-2-Clause-Patent */
struct EldenSmmuNative;
struct EldenSmmuNativeIrq {EldenSmmuNative *owner;unsigned index,intid;};
struct EldenSmmuNative {
    EldenSmmuShared core;EldenSmmuSharedConfig config;EldenSmmuNativeAuthority authority;
    EldenSmmuNativeMapping smmu,gic;EldenSmmuSharedLease leases[3];EldenSmmuNativeIrq irq[4];
    uint32_t attempted[4],registered[4],drained[4],inflight[4],captured[4],faulted_groups,global_fault,read_errors,spurious[4],status[4],syndrome[4];
    uint64_t address[3];uint32_t current_group,prepared;int error;
};
namespace {
static unsigned shared_native_attempted;
static int native_rc(int rc){return rc<=0?rc:-13;}
static int native_same(EldenSmmuNative *c,uint64_t owner,uint64_t epoch){return c&&owner==c->authority.owner&&epoch==c->authority.epoch?0:-116;}
static int native_admit(void *v,uint64_t owner,uint64_t epoch,int cleanup){auto *c=(EldenSmmuNative *)v;int rc=native_same(c,owner,epoch);if(rc)return rc;
    if(ml_at_interrupt_context()||!armcpu_gic_ready)return -13;
    int error=__atomic_load_n(&c->error,__ATOMIC_ACQUIRE);
    if(!cleanup&&(error||__atomic_load_n(&c->global_fault,__ATOMIC_ACQUIRE)||__atomic_load_n(&c->faulted_groups,__ATOMIC_ACQUIRE)))return error?error:-5;
    rc=native_rc(c->authority.admit(c->authority.context,owner,epoch,cleanup));
    if(!rc)rc=native_rc(c->authority.mapping(c->authority.context,owner,epoch,&c->smmu,&c->gic,cleanup));return rc;
}
static int native_early(void *v,uint64_t o,uint64_t e){auto *c=(EldenSmmuNative *)v;int rc=native_admit(c,o,e,0);
    return rc?rc:native_rc(c->authority.early(c->authority.context,o,e));}
static int native_idle(void *v,uint64_t o,uint64_t e,unsigned group){auto *c=(EldenSmmuNative *)v;int rc=native_admit(c,o,e,1);
    return rc?rc:group>=3?-22:native_rc(c->authority.quiesced(c->authority.context,o,e,group));}
static int native_memory(void *v,uint64_t o,uint64_t e,unsigned group,const EldenSmmuSharedMemory *m,int cleanup){auto *c=(EldenSmmuNative *)v;int rc=native_admit(c,o,e,cleanup);
    return rc?rc:group>=3?-22:native_rc(c->authority.memory(c->authority.context,o,e,group,m,cleanup));}
static int raw32(EldenSmmuNative *c,int gic,size_t off,uint32_t *out){const auto &m=gic?c->gic:c->smmu;
    if(m.bytes<4||off>m.bytes-4)return -22;return shared_native_read32(m.mapping,off,out);}
static int raw64(EldenSmmuNative *c,size_t off,uint64_t *out){if(c->smmu.bytes<8||off>c->smmu.bytes-8)return -22;return shared_native_read64(c->smmu.mapping,off,out);}
static int native_bank(void *v,uint64_t o,uint64_t e,unsigned group,const EldenSmmuSharedLease *lease,int cleanup){auto *c=(EldenSmmuNative *)v;int rc=native_admit(c,o,e,cleanup);if(rc)return rc;
    if(group>=3||!(c->config.groups&(1u<<group))||!lease||lease->bank<7||lease->bank>=83||!lease->asid||lease->asid>255||lease->route_epoch!=e)return -22;
    uint32_t actlr=0;rc=raw32(c,0,0x80000+4096*lease->bank+4,&actlr);
    if(!rc)rc=native_rc(c->authority.bank(c->authority.context,o,e,group,lease,actlr,cleanup));
    if(!rc){const auto &old=c->leases[group];
        if(c->attempted[group+1]){if(old.bank!=lease->bank||old.asid!=lease->asid||old.first_slot!=lease->first_slot||old.slots!=lease->slots||old.route_epoch!=lease->route_epoch||old.id0!=lease->id0||old.id1!=lease->id1||old.id2!=lease->id2)return -116;}
        else c->leases[group]=*lease;
        c->current_group=group;}return rc;
}
static int native_touch(void *v,uint64_t o,uint64_t e,uint32_t off,uint32_t base,uint32_t *out){auto *c=(EldenSmmuNative *)v;int rc=native_admit(c,o,e,1);
    return rc?rc:native_rc(c->authority.touch(c->authority.context,o,e,off,base,out));}
static int native_read32(void *v,size_t off,uint32_t *out){auto *c=(EldenSmmuNative *)v;int rc=native_admit(c,c->authority.owner,c->authority.epoch,1);if(rc)return rc;
    if(!(off<0x2000||(off>=0x80000&&off<0xd3000)))return -22;return raw32(c,0,off,out);}
static int native_read64(void *v,size_t off,uint64_t *out){auto *c=(EldenSmmuNative *)v;int rc=native_admit(c,c->authority.owner,c->authority.epoch,1);if(rc)return rc;
    if(off<0x80000||off>=0xd3000)return -22;unsigned reg=off&4095u;
    return reg==0x20||reg==0x28||reg==0x50||reg==0x60?raw64(c,off,out):-22;
}
static int native_write_admit(EldenSmmuNative *c,size_t off,uint32_t value,int wide){
    unsigned group=c->current_group;if(group>=3||!(c->config.groups&(1u<<group)))return -22;
    unsigned state=elden_smmu_boot_route_state();int early=state==EldenSmmuBootPreparing;
    int rc=native_admit(c,c->authority.owner,c->authority.epoch,!early);if(rc)return rc;
    if(early&&(rc=native_early(c,c->authority.owner,c->authority.epoch)))return rc;
    const auto &l=c->leases[group];if(l.bank<7||l.bank>=83)return -19;
    uint32_t cb=0x80000+4096*l.bank;int allowed=0;
    if(wide)return early&&(off==cb+0x20||off==cb+0x28)?0:-13;
    if(early){allowed=off==0x1000+4*l.bank||off==0x1800+4*l.bank||off==cb||off==cb+0x10||off==cb+0x30||off==cb+0x38||off==cb+0x3c||off==cb+0x58||off==cb+0x610||off==cb+0x618||off==cb+0x7f0;
        for(unsigned i=0;i<l.slots;i++)if(off==0x800+4*(l.first_slot+i)||off==0xc00+4*(l.first_slot+i))allowed=1;
    }else allowed=(off==cb&&!value)||off==cb+0x610||off==cb+0x7f0;
    return allowed?0:-13;
}
static int native_write32(void *v,size_t off,uint32_t value){auto *c=(EldenSmmuNative *)v;int rc=native_write_admit(c,off,value,0);
    return rc?rc:shared_native_write32(c->smmu.mapping,off,value);}
static int native_write64(void *v,size_t off,uint64_t value){auto *c=(EldenSmmuNative *)v;int rc=native_write_admit(c,off,0,1);
    return rc?rc:shared_native_write64(c->smmu.mapping,off,value);}
static int native_barrier(void *v){auto *c=(EldenSmmuNative *)v;int rc=native_admit(c,c->authority.owner,c->authority.epoch,1);if(!rc)shared_native_barrier();return rc;}
static uint64_t native_now(void *){return shared_native_now();}
static void native_fault(void *v){auto *cookie=(EldenSmmuNativeIrq *)v;auto *c=cookie->owner;unsigned i=cookie->index;
    __atomic_fetch_add(&c->inflight[i],1u,__ATOMIC_ACQUIRE);uint32_t status=UINT32_MAX;
    size_t cb=i?0x80000+4096*c->leases[i-1].bank:0;int rc=raw32(c,0,i?cb+0x58:0x48,&status);
    if(!rc&&!(status&(i?0xc00001feu:UINT32_MAX))){__atomic_fetch_add(&c->spurious[i],1u,__ATOMIC_RELAXED);}
    else {
        if(i)__atomic_fetch_or(&c->faulted_groups,1u<<(i-1),__ATOMIC_RELEASE);
        else __atomic_store_n(&c->global_fault,1u,__ATOMIC_RELEASE);
        uint32_t zero=0;if(!__atomic_compare_exchange_n(&c->captured[i],&zero,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
            __atomic_fetch_sub(&c->inflight[i],1u,__ATOMIC_RELEASE);return;}
        if(armcpu_spi_mask(cookie->intid,true))__atomic_fetch_or(&c->read_errors,1u<<(i+12),__ATOMIC_RELAXED);c->status[i]=status;
        if(rc)__atomic_fetch_or(&c->read_errors,1u<<i,__ATOMIC_RELAXED);
        if(raw32(c,0,i?cb+0x68:0x50,&c->syndrome[i]))__atomic_fetch_or(&c->read_errors,1u<<(i+4),__ATOMIC_RELAXED);
        if(i&&raw64(c,cb+0x60,&c->address[i-1]))__atomic_fetch_or(&c->read_errors,1u<<(i+8),__ATOMIC_RELAXED);
        __atomic_store_n(&c->captured[i],2u,__ATOMIC_RELEASE);
    }
    __atomic_fetch_sub(&c->inflight[i],1u,__ATOMIC_RELEASE);
}
static int native_irq_state(EldenSmmuNative *c,unsigned i,int masked){
    if(!__atomic_load_n(&c->registered[i],__ATOMIC_ACQUIRE))return -19;
    uint32_t id=c->irq[i].intid,word=id/32*4,bit=1u<<(id%32),ctl=0,en=0,pending=0;
    int rc=raw32(c,1,0,&ctl);if(!rc)rc=raw32(c,1,0x100+word,&en);if(!rc)rc=raw32(c,1,0x200+word,&pending);
    if(rc)return rc;if(ctl&(1u<<31))return -11;if(!!(en&bit)==!!masked||pending&bit||__atomic_load_n(&c->inflight[i],__ATOMIC_ACQUIRE))return -16;
    uint32_t fsr=0;size_t cb=i?0x80000+4096*c->leases[i-1].bank:0;rc=raw32(c,0,i?cb+0x58:0x48,&fsr);
    return rc?rc:fsr&(i?0xc00001feu:UINT32_MAX)?-5:0;
}
static int native_irq_mask_drain(EldenSmmuNative *c,unsigned i){
    if(__atomic_load_n(&c->drained[i],__ATOMIC_ACQUIRE))return 0;if(!__atomic_load_n(&c->registered[i],__ATOMIC_ACQUIRE))return c->attempted[i]?-19:0;
    IOReturn masked=armcpu_spi_mask(c->irq[i].intid,true);if(masked)return (int)masked;
    uint32_t id=c->irq[i].intid,word=id/32*4,bit=1u<<(id%32);uint64_t began=shared_native_now();
    for(unsigned tries=0;tries<2000;tries++){
        uint32_t ctl=0,en=0,active=0;int rc=raw32(c,1,0,&ctl);if(!rc)rc=raw32(c,1,0x100+word,&en);if(!rc)rc=raw32(c,1,0x300+word,&active);if(rc)return rc;
        if(!(ctl&(1u<<31))&&!(en&bit)&&!(active&bit)&&!__atomic_load_n(&c->inflight[i],__ATOMIC_ACQUIRE)){
            uint32_t fsr=0;size_t cb=i?0x80000+4096*c->leases[i-1].bank:0;rc=raw32(c,0,i?cb+0x58:0x48,&fsr);if(rc)return rc;
            if(fsr&(i?0xc00001feu:UINT32_MAX))return -5; // never acknowledge/suppress a latched SMMU fault
            rc=shared_native_write32(c->gic.mapping,0x280+word,bit);if(rc)return rc;shared_native_barrier();
            uint32_t pending=0;rc=raw32(c,1,0x200+word,&pending);if(rc)return rc;if(pending&bit)return -16;
            __atomic_store_n(&c->drained[i],1u,__ATOMIC_RELEASE);return 0;
        }
        uint64_t now=shared_native_now();if(now<began||now-began>=2000)return -110;IODelay(1);
    }return -110;
}
static int native_irq_register(EldenSmmuNative *c,unsigned i,unsigned intid){
    if(c->attempted[i])return -16;c->attempted[i]=1;c->irq[i]={c,i,intid};uint32_t observed=0;uint64_t router=0;
    IOReturn rc=armcpu_spi_attach(intid,false,native_fault,&c->irq[i],&observed,&router);if(rc)return (int)rc;
    __atomic_store_n(&c->registered[i],1u,__ATOMIC_RELEASE);
    int error=observed!=0xa003||router!=shared_native_affinity()?-71:native_irq_state(c,i,0);
    if(error)(void)armcpu_spi_mask(intid,true);return error;
}
static int native_irq(void *v,uint64_t o,uint64_t e,unsigned group,uint32_t bank,int action){auto *c=(EldenSmmuNative *)v;int rc=native_admit(c,o,e,action==0||action==3);if(rc)return rc;
    if(group>=3||!(c->config.groups&(1u<<group))||bank<7||bank>=83||action<0||action>3)return -22;
    unsigned i=group+1;if(action==1){
        if(c->leases[group].bank!=bank||c->leases[group].route_epoch!=e)return -116;
        if(!c->attempted[0]){rc=native_irq_register(c,0,EldenSmmuGlobalIntid);if(rc)return rc;}
        return native_irq_register(c,i,elden_smmu_context_intids[bank]);
    }
    if(c->leases[group].bank!=bank)return -116;
    if(action==2){rc=native_irq_state(c,0,0);if(!rc)rc=native_irq_state(c,i,0);return rc;}
    if(action==0)return native_irq_mask_drain(c,i);
    for(unsigned peer=0;peer<3;peer++)if(c->config.groups&(1u<<peer)){
        if(c->attempted[peer+1]&&!c->drained[peer+1])return -16;
        rc=native_idle(c,o,e,peer);if(rc)return rc;
        if(!c->attempted[peer+1])continue;
        uint32_t ctl=0,tlb=0;size_t cb=0x80000+4096*c->leases[peer].bank;
        rc=raw32(c,0,cb,&ctl);if(!rc)rc=raw32(c,0,cb+0x7f4,&tlb);if(rc)return rc;
        if(ctl&1||tlb&1)return -16;
    }
    return native_irq_mask_drain(c,0);
}
}
extern "C" int elden_smmu_native_open(const EldenSmmuSharedConfig *cfg,const EldenSmmuNativeAuthority *authority,
    const EldenSmmuNativeMapping *smmu,const EldenSmmuNativeMapping *gic,EldenSmmuNative **out){
    if(!cfg||!authority||!smmu||!gic||!out||*out||!authority->context||!authority->owner||!authority->epoch||cfg->owner!=authority->owner||cfg->epoch!=authority->epoch||
       !authority->admit||!authority->mapping||!authority->early||!authority->quiesced||!authority->memory||!authority->bank||!authority->touch||
       smmu->physical!=0x15000000||smmu->bytes!=0x100000||gic->physical!=0x16000000||gic->bytes!=0x10000||
       !smmu->mapping||!gic->mapping||(smmu->mapping|gic->mapping)&4095||smmu->mapping>UINTPTR_MAX-smmu->bytes||gic->mapping>UINTPTR_MAX-gic->bytes||
       (smmu->mapping<gic->mapping+gic->bytes&&gic->mapping<smmu->mapping+smmu->bytes)||ml_at_interrupt_context())return -22;
    int rc=native_rc(authority->admit(authority->context,authority->owner,authority->epoch,0));if(rc)return rc;
    if(elden_smmu_boot_route_state()!=EldenSmmuBootOpen)return -16;
    unsigned none=0;if(!__atomic_compare_exchange_n(&shared_native_attempted,&none,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -16;
    auto *c=(EldenSmmuNative *)IOMallocData(sizeof(EldenSmmuNative));if(!c)return -12;bzero(c,sizeof(*c));c->config=*cfg;c->authority=*authority;c->smmu=*smmu;c->gic=*gic;*out=c;
    EldenSmmuSharedOps ops={c,native_admit,native_early,native_idle,native_memory,native_irq,native_touch,native_now,
        {c,0x100000,native_read32,native_read64,native_write32,native_write64,native_barrier},native_bank};
    rc=elden_smmu_shared_open(&c->core,cfg,&ops);
    if(rc){__atomic_store_n(&c->error,rc,__ATOMIC_RELEASE);for(unsigned i=0;i<4;i++)if(c->registered[i])(void)armcpu_spi_mask(c->irq[i].intid,true);}
    else __atomic_store_n(&c->prepared,1u,__ATOMIC_RELEASE);return rc;
}
extern "C" int elden_smmu_native_shared(EldenSmmuNative *c,uint64_t owner,uint64_t epoch,EldenSmmuShared **out){
    int rc=native_same(c,owner,epoch);if(rc)return rc;if(!out||*out)return -22;
    rc=native_admit(c,owner,epoch,0);if(!rc&&!__atomic_load_n(&c->prepared,__ATOMIC_ACQUIRE))rc=-19;if(!rc)*out=&c->core;return rc;
}
extern "C" int elden_smmu_native_status(EldenSmmuNative *c,EldenSmmuNativeStatus *out){if(!c||!out||ml_at_interrupt_context())return -22;
    for(unsigned i=0;i<4;i++)if(__atomic_load_n(&c->inflight[i],__ATOMIC_ACQUIRE))return -11;
    EldenSmmuNativeStatus status={};int rc=elden_smmu_shared_status(&c->core,&status.shared);if(rc)return rc;
    status.prepared=__atomic_load_n(&c->prepared,__ATOMIC_ACQUIRE);status.error=__atomic_load_n(&c->error,__ATOMIC_ACQUIRE);
    status.global_registered=__atomic_load_n(&c->registered[0],__ATOMIC_ACQUIRE);status.global_drained=__atomic_load_n(&c->drained[0],__ATOMIC_ACQUIRE);
    status.faulted_groups=__atomic_load_n(&c->faulted_groups,__ATOMIC_ACQUIRE);status.global_fault=__atomic_load_n(&c->global_fault,__ATOMIC_ACQUIRE);status.read_errors=__atomic_load_n(&c->read_errors,__ATOMIC_ACQUIRE);
    for(unsigned i=0;i<4;i++){unsigned captured=__atomic_load_n(&c->captured[i],__ATOMIC_ACQUIRE);if(captured==1)return -11;
        status.inflight[i]=__atomic_load_n(&c->inflight[i],__ATOMIC_ACQUIRE);status.spurious[i]=__atomic_load_n(&c->spurious[i],__ATOMIC_RELAXED);
        if(captured==2){status.status[i]=c->status[i];status.syndrome[i]=c->syndrome[i];if(i)status.address[i-1]=c->address[i-1];}
        if(i){status.context_registered|=__atomic_load_n(&c->registered[i],__ATOMIC_ACQUIRE)<<(i-1);status.context_drained|=__atomic_load_n(&c->drained[i],__ATOMIC_ACQUIRE)<<(i-1);}}
    *out=status;return 0;
}
