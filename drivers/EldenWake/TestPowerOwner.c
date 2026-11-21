/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* HOST-ONLY synthetic provider peers — no physical panel, rail or system-PM
 * owner is present or modeled as evidence. Every provider callback is a test
 * double that proves the facade's fences, never that a real provider works. */
#include "PowerOwner.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct {
    EldenWakePowerOwner *f;uint64_t gen;
    const char *pname;uint64_t powner,pepoch;int oe_fail,oe_drift_once;
    unsigned oe_calls,admit_calls,request_calls,name_calls;
    int admit_fail,request_fail,reenter_rc;
    unsigned last_phase,last_gesture;uint64_t last_owner,last_epoch;
    int name_reenter,ready_in_admit,stop_in_admit,ready_in_request,stop_in_request,retire_in_request;
    int ready_in_resume,stop_in_resume;
    unsigned resumes;int resume_fail;uint64_t resume_owner,resume_epoch;
} Peer;
static const char *p_name(void *v){Peer *p=v;p->name_calls++;
    if(p->name_reenter)p->reenter_rc=elden_wake_power_ready(p->f,p->gen,p->name_reenter);
    return p->pname;}
static int p_oe(void *v,uint64_t *o,uint64_t *e){Peer *p=v;p->oe_calls++;
    if(p->oe_drift_once){p->oe_drift_once=0;*o=p->powner+1;*e=p->pepoch;return 0;}
    if(p->oe_fail)return -5;*o=p->powner;*e=p->pepoch;return 0;}
static int p_admit(void *v,uint64_t o,uint64_t e,unsigned phase){Peer *p=v;p->admit_calls++;
    p->last_owner=o;p->last_epoch=e;p->last_phase=phase;
    if(p->ready_in_admit){p->ready_in_admit=0;p->reenter_rc=elden_wake_power_ready(p->f,p->gen,1);}
    if(p->stop_in_admit){p->stop_in_admit=0;p->reenter_rc=elden_wake_power_stop(p->f,p->gen);}
    return p->admit_fail;}
static int p_request(void *v,uint64_t o,uint64_t e,unsigned g){Peer *p=v;p->request_calls++;
    p->last_owner=o;p->last_epoch=e;p->last_gesture=g;
    if(p->ready_in_request){p->ready_in_request=0;p->reenter_rc=elden_wake_power_ready(p->f,p->gen,1);}
    if(p->retire_in_request){p->retire_in_request=0;p->reenter_rc=elden_wake_power_retire(p->f,p->gen);}
    if(p->stop_in_request){p->stop_in_request=0;p->reenter_rc=elden_wake_power_stop(p->f,p->gen);}
    return p->request_fail;}
static int resume_path(void *v,uint64_t o,uint64_t e){Peer *p=v;p->resumes++;
    p->resume_owner=o;p->resume_epoch=e;
    if(p->ready_in_resume){p->ready_in_resume=0;p->reenter_rc=elden_wake_power_ready(p->f,p->gen,1);}
    if(p->stop_in_resume){p->stop_in_resume=0;p->reenter_rc=elden_wake_power_stop(p->f,p->gen);}
    return p->resume_fail;}
static int bind_peer(Peer *p,EldenWakePowerOwner *f){
    EldenWakePowerProvider pr={p,p_name,p_oe,p_admit,p_request};p->f=f;
    return elden_wake_power_bind(f,&pr,"panel0",resume_path,p,&p->gen);
}
static Peer *peer(void){Peer *p=calloc(1,sizeof(*p));assert(p);p->pname="panel0";p->powner=9;p->pepoch=3;return p;}
static int refusal_cases(void){
    Peer *p=peer();EldenWakePowerOwner f={0};uint64_t g=0;
    EldenWakePowerProvider pr={p,p_name,p_oe,p_admit,p_request};p->f=&f;
    CHECK(elden_wake_power_bind(0,&pr,"panel0",resume_path,p,&g)==-22);
    CHECK(elden_wake_power_bind(&f,0,"panel0",resume_path,p,&g)==-22);
    CHECK(elden_wake_power_bind(&f,&pr,0,resume_path,p,&g)==-22);
    CHECK(elden_wake_power_bind(&f,&pr,"",resume_path,p,&g)==-22);
    CHECK(elden_wake_power_bind(&f,&pr,"other",resume_path,p,&g)==-22); // name mismatch
    CHECK(elden_wake_power_bind(&f,&pr,"panel0",0,p,&g)==-22);          // no resume path
    CHECK(elden_wake_power_bind(&f,&pr,"panel0",resume_path,p,0)==-22);
    EldenWakePowerProvider b1={p,0,p_oe,p_admit,p_request};
    EldenWakePowerProvider b2={p,p_name,0,p_admit,p_request};
    EldenWakePowerProvider b3={p,p_name,p_oe,0,p_request};
    EldenWakePowerProvider b4={p,p_name,p_oe,p_admit,0};
    CHECK(elden_wake_power_bind(&f,&b1,"panel0",resume_path,p,&g)==-22);
    CHECK(elden_wake_power_bind(&f,&b2,"panel0",resume_path,p,&g)==-22);
    CHECK(elden_wake_power_bind(&f,&b3,"panel0",resume_path,p,&g)==-22);
    CHECK(elden_wake_power_bind(&f,&b4,"panel0",resume_path,p,&g)==-22);
    CHECK(!f.bound&&!g&&!p->oe_calls); // all above refuse before any probe
    p->pname=0;CHECK(elden_wake_power_bind(&f,&pr,"panel0",resume_path,p,&g)==-22);p->pname="panel0";
    p->oe_fail=1;CHECK(elden_wake_power_bind(&f,&pr,"panel0",resume_path,p,&g)==-5);p->oe_fail=0;
    p->powner=0;CHECK(elden_wake_power_bind(&f,&pr,"panel0",resume_path,p,&g)==-22);p->powner=9;
    p->pepoch=0;CHECK(elden_wake_power_bind(&f,&pr,"panel0",resume_path,p,&g)==-22);p->pepoch=3;
    CHECK(!f.bound);
    CHECK(elden_wake_power_bind(&f,&pr,"panel0",resume_path,p,&g)==0&&g==1);
    CHECK(f.bound&&f.owner==9&&f.epoch==3);
    CHECK(f.installed.context==&f&&f.installed.owner==9&&f.installed.epoch==3&&
          f.installed.admit&&f.installed.request_wake);
    CHECK(elden_wake_power_bind(&f,&pr,"panel0",resume_path,p,&g)==-22); // double bind
    free(p);return 0;
}
static int sequencing_cases(void){
    Peer *p=peer();EldenWakePowerOwner f={0};CHECK(bind_peer(p,&f)==0&&p->gen);
    EldenTouchWakePower *w=&f.installed;
    CHECK(w->admit(w->context,w->owner,w->epoch,EldenWakeAdmissionPaused)==0);
    CHECK(p->admit_calls==1&&p->last_owner==9&&p->last_epoch==3&&p->last_phase==EldenWakeAdmissionPaused);
    CHECK(w->admit(w->context,8,3,0)==-116&&p->admit_calls==1); // foreign args fenced first
    CHECK(w->admit(w->context,9,4,0)==-116&&p->admit_calls==1);
    CHECK(w->request_wake(w->context,8,3,15)==-116&&p->request_calls==0);
    p->admit_fail=-13;CHECK(w->admit(w->context,9,3,0)==-13);p->admit_fail=0;
    CHECK(w->request_wake(w->context,9,3,15)==0);
    CHECK(p->request_calls==1&&p->last_gesture==15&&f.pending==1&&f.requests==1);
    CHECK(w->request_wake(w->context,9,3,15)==-16);          // duplicate while one pending
    CHECK(p->request_calls==1&&f.pending==1);                // provider never asked again
    CHECK(elden_wake_power_ready(&f,p->gen,2)==-22);           // bad result arg
    CHECK(elden_wake_power_ready(&f,p->gen+7,1)==-116&&f.pending==1); // stale token
    CHECK(elden_wake_power_ready(&f,p->gen,0)==0);             // explicit not-ready consumes
    CHECK(f.pending==0&&f.not_ready==1&&f.readies==1&&p->resumes==0);
    p->request_fail=-5;CHECK(w->request_wake(w->context,9,3,15)==-5&&f.pending==0);p->request_fail=0;
    CHECK(elden_wake_power_ready(&f,p->gen,1)==-16);           // out-of-order, none pending
    CHECK(elden_wake_power_ready(&f,p->gen,0)==-16);           // duplicate not-ready too
    CHECK(w->request_wake(w->context,9,3,15)==0&&f.pending==1);
    CHECK(elden_wake_power_ready(&f,p->gen,1)==0);             // async ready -> resume
    CHECK(p->resumes==1&&p->resume_owner==9&&p->resume_epoch==3&&f.pending==0&&f.resumes==1);
    CHECK(elden_wake_power_ready(&f,p->gen,1)==-16);           // duplicate ready refused
    p->resume_fail=-13;
    CHECK(w->request_wake(w->context,9,3,15)==0);
    CHECK(elden_wake_power_ready(&f,p->gen,1)==-13);           // actual resume error propagates
    CHECK(f.pending==0&&f.resumes==2&&p->resumes==2);p->resume_fail=0;
    free(p);return 0;
}
static int drift_cases(void){
    Peer *p=peer();EldenWakePowerOwner f={0};CHECK(bind_peer(p,&f)==0);
    EldenTouchWakePower *w=&f.installed;
    CHECK(w->request_wake(w->context,9,3,15)==0&&f.pending==1);
    p->oe_drift_once=1;
    CHECK(elden_wake_power_ready(&f,p->gen,1)==-116);          // owner drift fenced
    CHECK(f.pending==1&&f.readies==0&&p->resumes==0);          // request not consumed
    CHECK(elden_wake_power_ready(&f,p->gen,1)==0&&p->resumes==1); // restored identity ok
    p->oe_drift_once=1;CHECK(w->admit(w->context,9,3,0)==-116);
    CHECK(p->admit_calls==0);                                  // probe drift -> no provider I/O
    p->oe_drift_once=1;CHECK(w->request_wake(w->context,9,3,15)==-116);
    CHECK(p->request_calls==1&&f.pending==0);
    p->oe_fail=1;CHECK(elden_wake_power_ready(&f,p->gen,1)==-116);p->oe_fail=0;
    free(p);return 0;
}
static int reentry_cases(void){
    Peer *p=peer();EldenWakePowerOwner f={0};CHECK(bind_peer(p,&f)==0);
    EldenTouchWakePower *w=&f.installed;
    p->ready_in_request=1;                                     // synchronous self-ack refused
    CHECK(w->request_wake(w->context,9,3,15)==0);
    CHECK(p->reenter_rc==-16&&f.pending==1&&f.resumes==0);
    CHECK(elden_wake_power_ready(&f,p->gen,1)==0&&f.resumes==1); // own thread works
    p->ready_in_admit=1;
    CHECK(w->admit(w->context,9,3,0)==0&&p->reenter_rc==-16);
    CHECK(w->request_wake(w->context,9,3,15)==0);
    p->ready_in_resume=1;
    CHECK(elden_wake_power_ready(&f,p->gen,1)==0);             // resume ran, inner ready refused
    CHECK(p->reenter_rc==-16&&f.pending==0&&f.resumes==2);
    p->retire_in_request=1;                                    // retire while callback active
    CHECK(w->request_wake(w->context,9,3,15)==0);
    CHECK(p->reenter_rc==-16&&f.bound==1&&f.pending==1);
    p->stop_in_admit=1;                                        // deferred stop inside admit
    CHECK(w->admit(w->context,9,3,0)==-125);
    CHECK(p->reenter_rc==0&&f.stopped==1);
    CHECK(w->admit(w->context,9,3,0)==-125);                  // callbacks after stop
    CHECK(w->request_wake(w->context,9,3,15)==-125);
    CHECK(elden_wake_power_ready(&f,p->gen,1)==-125);
    free(p);return 0;
}
static int lifecycle_cases(void){
    Peer *p=peer();EldenWakePowerOwner f={0};CHECK(bind_peer(p,&f)==0);
    EldenTouchWakePower *w=&f.installed;
    CHECK(w->request_wake(w->context,9,3,15)==0&&f.pending==1);
    CHECK(elden_wake_power_retire(&f,p->gen)==-16);            // requires prior stop
    CHECK(elden_wake_power_stop(&f,p->gen)==0&&f.stopped==1&&f.stops==1);
    CHECK(elden_wake_power_stop(&f,p->gen)==-125);             // idempotent
    CHECK(elden_wake_power_retire(&f,p->gen+9)==-116);
    CHECK(elden_wake_power_retire(&f,p->gen)==0);
    CHECK(!f.bound&&!f.stopped&&!f.installed.admit&&!f.owner&&!f.pending);
    uint64_t old=p->gen;
    CHECK(elden_wake_power_ready(&f,old,1)==-116);            // retired generation stale
    CHECK(elden_wake_power_stop(&f,old)==-116);
    CHECK(bind_peer(p,&f)==0&&p->gen&&p->gen!=old);             // rebind fresh generation
    w=&f.installed;
    CHECK(w->admit(w->context,9,3,0)==0);
    CHECK(w->request_wake(w->context,9,3,15)==0&&f.pending==1);
    CHECK(elden_wake_power_ready(&f,old,1)==-116);            // old gen can never resume
    CHECK(elden_wake_power_ready(&f,p->gen,1)==0&&p->resumes==1);
    p->stop_in_request=1;                                      // deferred stop inside request
    CHECK(w->request_wake(w->context,9,3,15)==-125);
    CHECK(p->reenter_rc==0&&f.stopped==1&&f.pending==1);       // queued work recorded; ready fenced
    free(p);p=peer();EldenWakePowerOwner f2={0};CHECK(bind_peer(p,&f2)==0);
    w=&f2.installed;CHECK(w->request_wake(w->context,9,3,15)==0&&f2.pending==1);
    p->stop_in_resume=1;                                       // deferred stop inside resume cb
    CHECK(elden_wake_power_ready(&f2,p->gen,1)==-125);
    CHECK(p->reenter_rc==0&&f2.stopped==1&&f2.pending==0&&f2.resumes==1);
    free(p);return 0;
}
static int callback_fence_cases(void){
    Peer *p=peer();EldenWakePowerOwner f={0};
    p->name_reenter=1;                                          // name cb reenters a facade op
    CHECK(bind_peer(p,&f)==0);
    CHECK(p->name_calls==1&&p->reenter_rc==-116);               // refused: no recursive entry
    CHECK(f.bound==1);
    free(p);return 0;
}
static int exhaustion_cases(void){
    Peer *p=peer();EldenWakePowerOwner f={0};p->f=&f;
    EldenWakePowerProvider pr={p,p_name,p_oe,p_admit,p_request};
    atomic_store(&f.seq,UINT64_MAX);uint64_t g=77;
    CHECK(elden_wake_power_bind(&f,&pr,"panel0",resume_path,p,&g)==-22);
    CHECK(g==77);                                               // out-param untouched
    CHECK(f.generation==0&&!f.bound&&!f.installed.admit);       // nothing armed
    CHECK(p->name_calls==0&&p->oe_calls==0);                    // refused before provider I/O
    CHECK(elden_wake_power_bind(&f,&pr,"panel0",resume_path,p,&g)==-22); // stays refused
    CHECK(f.seq==UINT64_MAX&&f.generation==0);                  // no wrap, no reuse
    atomic_store(&f.seq,0);
    CHECK(bind_peer(p,&f)==0&&f.generation==1);
    CHECK(elden_wake_power_stop(&f,p->gen)==0&&elden_wake_power_retire(&f,p->gen)==0);
    CHECK(f.generation==0);
    atomic_store(&f.seq,UINT64_MAX);
    CHECK(elden_wake_power_bind(&f,&pr,"panel0",resume_path,p,&g)==-22);
    CHECK(f.generation==0&&f.seq==UINT64_MAX);                  // old value remains, none reused
    free(p);return 0;
}
static int status_cases(void){
    Peer *p=peer();EldenWakePowerOwner f={0};
    EldenWakePowerStatus st;CHECK(elden_wake_power_status(&f,0)==-22);
    CHECK(elden_wake_power_status(0,&st)==-22);
    CHECK(elden_wake_power_status(&f,&st)==0&&!st.bound&&!st.stopped);
    CHECK(bind_peer(p,&f)==0);
    EldenTouchWakePower *w=&f.installed;
    CHECK(w->admit(w->context,9,3,0)==0);
    CHECK(w->request_wake(w->context,9,3,15)==0);
    CHECK(elden_wake_power_ready(&f,p->gen,1)==0);
    CHECK(elden_wake_power_ready(&f,p->gen,1)==-16);
    CHECK(elden_wake_power_status(&f,&st)==0);
    CHECK(st.bound&&st.generation==p->gen&&st.owner==9&&st.epoch==3);
    CHECK(st.admits==1&&st.requests==1&&st.readies==1&&st.resumes==1&&st.pending==0);
    CHECK(st.rejected==1&&st.not_ready==0&&st.stops==0);
    free(p);return 0;
}
int main(void){
    int rc;
    if((rc=refusal_cases())||(rc=sequencing_cases())||(rc=drift_cases())||
       (rc=reentry_cases())||(rc=lifecycle_cases())||(rc=callback_fence_cases())||
       (rc=exhaustion_cases())||(rc=status_cases()))return rc;
    printf("TestPowerOwner: %d checks passed\n",checks);return 0;
}
