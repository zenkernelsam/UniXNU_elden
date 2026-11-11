/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Lifecycle.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
int main(){
    EldenBlLifecycle closed={};elden_bl_worker_stop(&closed);
    assert(elden_bl_worker_done(&closed)&&!elden_bl_worker_claim(&closed));
    EldenBlLifecycle queued={};assert(elden_bl_worker_claim(&queued));
    elden_bl_worker_stop(&queued);assert(!elden_bl_worker_done(&queued));
    assert(elden_bl_worker_stopping(&queued));elden_bl_worker_finish(&queued);assert(elden_bl_worker_done(&queued));
    for(unsigned n=0;n<1000;++n){
        EldenBlLifecycle s={};std::atomic<unsigned> ready{0},start{0},owner{0},completed{0};
        std::thread worker([&]{
            ++ready;while(!start.load())std::this_thread::yield();
            if(elden_bl_worker_claim(&s)){
                assert(++owner==1);assert(!elden_bl_worker_claim(&s));
                if(!elden_bl_worker_stopping(&s))std::this_thread::yield();
                /* Cleanup must publish before terminal state, even when stop
                 * raced with a previously admitted transaction. */
                completed=1;elden_bl_worker_finish(&s);
            }
        });
        std::thread stop([&]{++ready;while(!start.load())std::this_thread::yield();elden_bl_worker_stop(&s);});
        while(ready.load()!=2)std::this_thread::yield();start=1;worker.join();stop.join();
        assert(elden_bl_worker_done(&s)&&elden_bl_worker_stopping(&s)&&!elden_bl_worker_claim(&s));
        assert(owner.load()==0||completed.load()==1);
    }
    puts("PASS: 1000 lifecycle races, stop before queue, one owner, cleanup before terminal");
}
