// Tests the actual header used by the native SDAM/RPC implementation.
#include "EldenRebootOwnership.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
int main() {
    volatile unsigned state=0;
    assert(elden_reboot_request(&state));
    assert(elden_reboot_deadline_commit(&state));
    assert(!elden_reboot_ack_cancel(&state));
    assert(!elden_reboot_ack_commit(&state));
    assert(state==2);
    for(unsigned initial: {0u,1u,2u,3u,255u}) {
        state=initial;bool won=elden_reboot_deadline_commit(&state);
        assert(won==(initial<2));assert(state==(initial<2?2:initial));
    }
    for(unsigned i=0;i<1000;++i) {
        state=0;std::atomic<bool> go=false;std::atomic<unsigned> commits=0;
        std::thread rpc([&]{while(!go.load()){};if(elden_reboot_request(&state)&&elden_reboot_ack_commit(&state))++commits;});
        std::thread deadline([&]{while(!go.load()){};if(elden_reboot_deadline_commit(&state))++commits;});
        std::thread cancel([&]{while(!go.load()){};elden_reboot_ack_cancel(&state);});
        go=true;rpc.join();deadline.join();cancel.join();
        assert(commits==1);assert(state==2);assert(!elden_reboot_ack_cancel(&state));
    }
    puts("PASS: 1000 ACK/deadline/cancel races; exactly one commit, late cancellation cannot undo it");
}
