/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Exercise the real ADC and BT readers against one contended EE0 bus.
 * This is an injected bus model, not proof of the hardware's drop cause. */
#include "Spmi.h"
#include "BtPmic.h"
#include "EldenAdcResources.h"
#include "TransactionGate.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
static atomic_uint busy,collisions,completed,refused;
static unsigned forced_status;
static pthread_barrier_t start;
typedef struct {unsigned owner,polls;bool collided;} Client;
static void pause_us(void *p,unsigned us){(void)p;assert(us==1);sched_yield();}
static void command(Client *c){
    unsigned empty=0;c->polls=0;
    c->collided=!atomic_compare_exchange_strong(&busy,&empty,c->owner);
    if(c->collided)atomic_fetch_add(&collisions,1);
    /* Give the independent client a chance to submit its command. */
    struct timespec delay={0,100000};nanosleep(&delay,0);
}
static uint32_t status(Client *c){
    if(forced_status==2)return 0;
    if(++c->polls<8)return 0;
    if(forced_status==9){atomic_store(&busy,0);return 9;}
    return c->collided?9:1;
}
static uint32_t data(Client *c){assert(!c->collided);assert(atomic_exchange(&busy,0)==c->owner);atomic_fetch_add(&completed,1);return 0x55;}
static uint32_t adc_read(void *p,enum EldenAdcSpmiSpace s,uint32_t off){
    Client *c=p;
    if(s==EldenAdcSpmiCore)return off==0?ELDEN_ADC_SPMI_VERSION:off==0x2000+4*591?0x9001:0;
    if(s==EldenAdcSpmiOwner)return 0;
    assert(s==EldenAdcSpmiObserver);return off==8?status(c):off==0x18?data(c):0;
}
static void adc_write(void *p,enum EldenAdcSpmiSpace s,uint32_t off,uint32_t value){assert(s==EldenAdcSpmiObserver&&off==0&&value==0x08000450);command(p);}
static uint32_t bt_read(void *p,uint32_t off){assert(off==629*32+8||off==629*32+0x18);return off==629*32+8?status(p):data(p);}
static void bt_write(void *p,uint32_t off,uint32_t value){assert(off==629*32&&value==0x08000040);command(p);}
static void *worker(void *arg){
    unsigned owner=(unsigned)(uintptr_t)arg;Client c={.owner=owner};
    EldenAdcSpmi adc={.io={&c,adc_read,adc_write,pause_us},.target=EldenSpmiAdcSdam,.apid=591,.selected=true};
    PbSpmiIo bt={.ctx=&c,.obs_read=bt_read,.obs_write=bt_write,.delay_us=pause_us};
    pthread_barrier_wait(&start);
    for(unsigned i=0;i<2000;i++){
        uint8_t value=0xa5;int rc=owner==1?elden_adc_spmi_read(&adc,0x45,&value,1):pb_spmi_read8(&bt,629,4,&value);
        assert(rc==0||rc==-5||rc==-16);
        if(rc==-16)atomic_fetch_add(&refused,1);
        if(!rc)assert(value==0x55);else assert(value==0xa5);
    }
    return 0;
}
int main(int argc,char **argv){
    assert(argc==2);
    if(argv[1][0]=='e'||argv[1][0]=='t'){
        Client c={.owner=1},b={.owner=2};
        EldenAdcSpmi adc={.io={&c,adc_read,adc_write,pause_us},.target=EldenSpmiAdcSdam,.apid=591,.selected=true};
        PbSpmiIo bt={.ctx=&b,.obs_read=bt_read,.obs_write=bt_write,.delay_us=pause_us};
        uint8_t value=0xa5;forced_status=argv[1][0]=='e'?9:2;
        assert(elden_adc_spmi_read(&adc,0x45,&value,1)==(forced_status==9?-5:-110));assert(value==0xa5);
        forced_status=0;
        if(argv[1][0]=='e'){
            assert(elden_spmi_transaction_state()==0);
            assert(!pb_spmi_read8(&bt,629,4,&value)&&value==0x55);
        }else{
            assert(elden_spmi_transaction_state()==UINT32_MAX);
            assert(pb_spmi_read8(&bt,629,4,&value)==-5&&value==0xa5&&b.polls==0);
        }
        puts("PASS actual readers: completed error releases; timeout prevents subsequent command");return 0;
    }
    assert(!pthread_barrier_init(&start,0,2));pthread_t a,b;
    assert(!pthread_create(&a,0,worker,(void *)(uintptr_t)1)&&!pthread_create(&b,0,worker,(void *)(uintptr_t)2));
    assert(!pthread_join(a,0)&&!pthread_join(b,0));assert(!atomic_load(&busy));
    printf("collisions=%u completed=%u refused-before-command=%u\n",atomic_load(&collisions),atomic_load(&completed),atomic_load(&refused));fflush(stdout);
    if(argv[1][0]=='f')assert(atomic_load(&collisions)==0&&atomic_load(&completed)+atomic_load(&refused)==4000&&atomic_load(&completed)>0);
    else assert(atomic_load(&collisions)>0);
    pthread_barrier_destroy(&start);
}
