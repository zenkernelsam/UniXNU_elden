/* SPDX-License-Identifier: MIT */
extern "C" void native_adreno_close_gate(void) {eldenGpuClose(&eldenGate);}
extern "C" IOReturn native_adreno_quiesce_for_reboot(uint32_t budget) {
    if(!budget||budget>2000000)return kIOReturnBadArgument;
    eldenGpuClose(&eldenGate);
    if(!g.published)return kIOReturnSuccess;
    if(!g.lock||!g.submit||!g.clock.now_us)return kIOReturnNotReady;
    uint64_t start=g.clock.now_us(g.clock.cookie),last=start;
    for(unsigned polls=0;polls<2001;++polls) {
        uint64_t now=g.clock.now_us(g.clock.cookie);
        if(now<last||now-start>=budget)return kIOReturnTimeout;last=now;
        if(eldenGpuEmpty(&eldenGate)&&IOLockTryLock(g.lock)) {
            IOReturn rc=g.submit->waitAll(&g.clock,uint32_t(budget-(now-start)));
            if(!rc&&g.submit->outstanding())rc=kIOReturnBusy;
            uint64_t finished=g.clock.now_us(g.clock.cookie);
            if(!rc&&(finished<now||finished-start>=budget))rc=kIOReturnTimeout;
            IOLockUnlock(g.lock);return rc;
        }
        IOSleep(1);
    }
    return kIOReturnTimeout;
}
