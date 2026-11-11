#include <cstdint>
#include <cassert>
#include <cstdio>
#include UFS_POLL_HEADER
struct Port {
    unsigned db=1,is=0,ocs=15,mode=0,doorbells=0,invalidates=0;
    uint32_t doorbell(){++doorbells;return db;}
    uint32_t interrupt(){return is;}
    uint32_t completedOcs(){++invalidates;return ocs;}
    bool expired(){
        // Simulate completion during a deschedule between the first MMIO
        // observation and the deadline check, without resubmitting anything.
        if(mode==1){db=0;ocs=0;}
        if(mode==2){db=0;ocs=15;}
        if(mode==3){db=0;ocs=0;is=0x80;}
        return true;
    }
    void delay(){assert(false);}
};
int main(){
    {Port p;p.db=0;p.ocs=0;UfsTransferPoll s;assert(pollUfsTransfer(p,s)==UfsTransferEnd::Completed);assert(p.doorbells==1);}
    {Port p;p.mode=1;UfsTransferPoll s;
#if ORIGINAL_POLICY
     assert(pollUfsTransfer(p,s)==UfsTransferEnd::Timeout);assert(p.doorbells==1);
#else
     assert(pollUfsTransfer(p,s)==UfsTransferEnd::Completed);assert(p.doorbells==2 && s.rechecked && s.ocs==0);
#endif
    }
    {Port p;UfsTransferPoll s;assert(pollUfsTransfer(p,s)==UfsTransferEnd::Timeout);assert(s.ocs==15);}
    {Port p;p.mode=2;UfsTransferPoll s;assert(pollUfsTransfer(p,s)==UfsTransferEnd::Timeout);assert(s.ocs==15);}
    {Port p;p.mode=3;UfsTransferPoll s;
#if ORIGINAL_POLICY
     assert(pollUfsTransfer(p,s)==UfsTransferEnd::Timeout);
#else
     assert(pollUfsTransfer(p,s)==UfsTransferEnd::Fatal);
#endif
    }
    {Port p;p.db=0;p.ocs=0;p.is=0x80;UfsTransferPoll s;assert(pollUfsTransfer(p,s)==UfsTransferEnd::Fatal);assert(!p.invalidates);}
    {Port p;p.db=0;p.ocs=4;UfsTransferPoll s;assert(pollUfsTransfer(p,s)==UfsTransferEnd::Completed);assert(s.ocs==4);}
    std::puts("PASS: completion/deadline race, pending command, invalid OCS, fatal status and nonzero OCS boundaries");
}
