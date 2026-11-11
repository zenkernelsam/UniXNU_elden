#include "PulseControl.h"
#include <thread>
#include <atomic>
#include <cassert>
#include <cstdio>
int main(){
    volatile unsigned state=0;elden_thermal_worker_close_idle(&state);assert(state==2&&!elden_thermal_worker_claim(&state));
    for(unsigned n=0;n<1000;n++){
        state=0;std::atomic<bool> go=false;bool started=false;
        std::thread a([&]{while(!go.load()){};started=elden_thermal_worker_claim(&state);});
        std::thread b([&]{while(!go.load()){};elden_thermal_worker_close_idle(&state);});
        go=true;a.join();b.join();
        assert(state==(started?1u:2u));
        if(started)__atomic_store_n(&state,2u,__ATOMIC_RELEASE);
        assert(!elden_thermal_worker_claim(&state));
    }
    puts("PASS: 1000 queued-start/stop races; terminal state refuses late hardware startup");
}
