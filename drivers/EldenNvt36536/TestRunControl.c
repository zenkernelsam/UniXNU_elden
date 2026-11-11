/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "RunControl.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
static void *worker(void *p){
    EldenTouchRunControl *c=p;
    if(elden_touch_run_queue(c)){
        (void)elden_touch_run_enter(c);
        elden_touch_run_complete(c,false);
    }
    return NULL;
}
int main(void){
    EldenTouchRunControl c={0};
    assert(elden_touch_run_request_stop(&c)==EldenTouchStopped);
    assert(!elden_touch_run_queue(&c)&&!elden_touch_run_enter(&c));
    c.state=0;assert(elden_touch_run_queue(&c));
    assert(elden_touch_run_request_stop(&c)==EldenTouchStopping&&!elden_touch_run_enter(&c));
    elden_touch_run_complete(&c,false);assert(elden_touch_run_state(&c)==EldenTouchStopped);
    c.state=0;assert(elden_touch_run_queue(&c)&&elden_touch_run_enter(&c));
    assert(elden_touch_run_request_stop(&c)==EldenTouchStopping);
    elden_touch_run_complete(&c,true);assert(elden_touch_run_request_stop(&c)==EldenTouchHeld);
    for(unsigned n=0;n<1000;n++){
        c.state=0;pthread_t t;assert(!pthread_create(&t,NULL,worker,&c));
        unsigned s=elden_touch_run_request_stop(&c);assert(s==EldenTouchStopping||s==EldenTouchStopped);
        assert(!pthread_join(t,NULL));assert(elden_touch_run_state(&c)==EldenTouchStopped);
        assert(!elden_touch_run_queue(&c));
    }
    puts("PASS touch stop lifecycle and1000 queue/stop races; no hardware simulation");
}
